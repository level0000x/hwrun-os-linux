/*
 * hwrun.c — HWRun OS 核心库实现
 *
 * 提供 hwrun 基础工具函数与参数树操作。
 * 依赖链第 0 环：其它所有模块共享的基础函数库。
 */

#include "hwrun.h"

#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>

char *hw_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

char *hw_strndup(const char *s, size_t n) {
    if (!s) return NULL;
    char *p = malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

int hw_str_eq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    return strcmp(a, b) == 0;
}

char **hw_str_split(const char *s, const char *sep, int *count) {
    if (!s || !sep) { if (count) *count = 0; return NULL; }

    /* 先统计 */
    int n = 0;
    char *tmp = hw_strdup(s);
    char *save = NULL;
    for (char *tok = strtok_r(tmp, sep, &save); tok;
         tok = strtok_r(NULL, sep, &save)) {
        n++;
    }
    free(tmp);

    if (n == 0) { if (count) *count = 0; return NULL; }

    char **list = malloc((n + 1) * sizeof(char *));
    if (!list) { if (count) *count = 0; return NULL; }

    tmp = hw_strdup(s);
    save = NULL;
    int i = 0;
    for (char *tok = strtok_r(tmp, sep, &save); tok;
         tok = strtok_r(NULL, sep, &save)) {
        list[i++] = hw_strdup(tok);
    }
    list[i] = NULL;
    free(tmp);

    if (count) *count = i;
    return list;
}

void hw_str_list_free(char **list, int count) {
    if (!list) return;
    for (int i = 0; i < count; i++) free(list[i]);
    free(list);
}

int hw_fmt_path(char *out, size_t cap, const char *dir, const char *name) {
    if (!out || cap == 0 || !dir || !name) return HWRUN_EINVAL;
    if (dir[strlen(dir)-1] == '/')
        return snprintf(out, cap, "%s%s", dir, name) < (int)cap ? HWRUN_OK : HWRUN_ENOMEM;
    else
        return snprintf(out, cap, "%s/%s", dir, name) < (int)cap ? HWRUN_OK : HWRUN_ENOMEM;
}

/* 负 errno -> 可读描述；HWRun 私有域（HW_EBASE 之上）-> 固定文案 */
const char *hw_strerror(int rc) {
    if (rc == 0) return "ok";
    int code = (rc < 0) ? -rc : rc;
    if (code >= HW_EBASE) {
        switch (code - HW_EBASE) {
        case 1: return "conflict (dependency or registration)";
        case 2: return "not ready";
        default: return "unknown error";
        }
    }
    return strerror(code);   /* 标准 errno（glibc 线程安全，返回静态缓冲） */
}

/* ============================================================
 * 参数树
 * ============================================================ */

static hw_param_t *param_alloc(const char *key, const char *value,
                               int type, const char *desc) {
    hw_param_t *p = calloc(1, sizeof(hw_param_t));
    if (!p) return NULL;
    snprintf(p->key, sizeof(p->key), "%s", key);
    if (value) snprintf(p->value, sizeof(p->value), "%s", value);
    p->type = type;
    if (desc) snprintf(p->description, sizeof(p->description), "%s", desc);
    return p;
}

/* 按 '.' 分割 key 的前缀与剩余 */
static int param_split_key(const char *key, char *head, size_t hcap,
                           const char **rest) {
    const char *dot = strchr(key, '.');
    if (!dot) {
        snprintf(head, hcap, "%s", key);
        *rest = NULL;
        return 0;
    }
    size_t hlen = dot - key;
    if (hlen >= hcap) hlen = hcap - 1;
    memcpy(head, key, hlen);
    head[hlen] = '\0';
    *rest = dot + 1;
    return 0;
}

hw_param_t *hw_param_get_child(hw_param_t *p, const char *key) {
    for (hw_param_t *c = p->child; c; c = c->sibling) {
        if (hw_str_eq(c->key, key)) return c;
    }
    return NULL;
}

hw_param_t *hw_param_add_child(hw_param_t *parent, const char *key,
                               const char *value, int type, const char *desc) {
    if (!parent || !key) return NULL;
    hw_param_t *p = param_alloc(key, value, type, desc);
    if (!p) return NULL;
    /* 追加到兄弟链表尾部 */
    hw_param_t **tail = &parent->child;
    while (*tail) tail = &(*tail)->sibling;
    *tail = p;
    return p;
}

hw_param_t *hw_param_find(hw_param_t *root, const char *key) {
    if (!root || !key) return NULL;
    char head[128];
    const char *rest;
    param_split_key(key, head, sizeof(head), &rest);

    hw_param_t *node = hw_param_get_child(root, head);
    if (!node) return NULL;
    if (!rest) return node;
    return hw_param_find(node, rest);
}

const char *hw_param_value(hw_param_t *root, const char *key,
                           const char *def) {
    hw_param_t *p = hw_param_find(root, key);
    if (!p || !p->value[0]) return def;
    return p->value;
}

int hw_param_set(hw_param_t *root, const char *key, const char *value,
                 int type, const char *desc) {
    if (!root || !key || !value) return HWRUN_EINVAL;

    char head[128];
    const char *rest;
    param_split_key(key, head, sizeof(head), &rest);

    hw_param_t *node = hw_param_get_child(root, head);
    if (!node) {
        node = hw_param_add_child(root, head, NULL, type, desc);
        if (!node) return HWRUN_ENOMEM;
    }

    if (!rest) {
        snprintf(node->value, sizeof(node->value), "%s", value);
        node->type = type;
        return HWRUN_OK;
    }

    /* 递归：create 中间的父节点 */
    if (!node->child) {
        /* 占位父节点（无值） */
    }
    hw_param_t *tail_hook = node;
    return hw_param_set(tail_hook, rest, value, type, desc);
}

/* 打印一棵参数树（调试/CLI 用） */
void hw_param_dump(hw_param_t *p, int indent) {
    if (!p) return;
    for (int i = 0; i < indent; i++) fputs("  ", stdout);
    if (p->child && p->value[0]) {
        printf("[%s] = %s\n", p->key, p->value);
    } else if (p->child) {
        printf("[%s]\n", p->key);
    } else {
        printf("%s = %s\n", p->key, p->value[0] ? p->value : "<nil>");
    }
    for (hw_param_t *c = p->child; c; c = c->sibling) {
        hw_param_dump(c, indent + 1);
    }
}