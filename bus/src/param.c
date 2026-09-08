/*
 * param.c — PARAM 参数系统完整实现
 *
 * 依赖链第 1 环。参数树 + 监听 + 文件加载保存。
 * 参数树的底层操作复用 hwrun.h 提供的 hw_param_set / hw_param_add_child。
 */

#include "param.h"

#include <stdio.h>
#include <ctype.h>
#include <fnmatch.h>

static int hw_param_tree_save(const hw_param_t *p, const char *prefix, FILE *fp);

int hw_param_init(hw_param_context_t *ctx, const char *root_dir) {
    if (!ctx) return HWRUN_EINVAL;
    memset(ctx, 0, sizeof(*ctx));
    hw_locker_init(&ctx->lock, HWLOCK_RW);
    ctx->root = calloc(1, sizeof(hw_param_t));
    if (!ctx->root) {
        hw_locker_destroy(&ctx->lock);
        return HWRUN_ENOMEM;
    }
    snprintf(ctx->root->key, sizeof(ctx->root->key), ".");
    ctx->root->type = HWPARAM_TYPE_STRING;
    ctx->initialized = 1;
    if (root_dir) {
        hw_fmt_path(ctx->dirs[HWPARAM_USER], sizeof(ctx->dirs[HWPARAM_USER]), root_dir, "params");
        hw_fmt_path(ctx->dirs[HWPARAM_HW], sizeof(ctx->dirs[HWPARAM_HW]), root_dir, "hardware");
        hw_fmt_path(ctx->dirs[HWPARAM_GIT], sizeof(ctx->dirs[HWPARAM_GIT]), root_dir, "git");
    }
    return HWRUN_OK;
}

void hw_param_tree_free(hw_param_t *p) {
    if (!p) return;
    hw_param_t *c = p->child;
    while (c) {
        hw_param_t *n = c->sibling;
        hw_param_tree_free(c);
        c = n;
    }
    free(p);
}

void hw_param_shutdown(hw_param_context_t *ctx) {
    if (!ctx) return;
    /* 释放 watcher 链表（watch 节点由 hw_param_watch 分配，shutdown 前不回收则泄漏） */
    hw_param_watcher_t *w = ctx->watchers;
    while (w) {
        hw_param_watcher_t *n = w->next;
        free(w);
        w = n;
    }
    ctx->watchers = NULL;
    hw_param_tree_free(ctx->root);
    ctx->root = NULL;
    ctx->initialized = 0;
    hw_locker_destroy(&ctx->lock);
}

const char *hw_param_get(hw_param_context_t *ctx, const char *key) {
    if (!ctx || !ctx->root || !key) return NULL;
    const char *ret = NULL;
    HW_RDLOCK_GUARD(&ctx->lock) {
        hw_param_t *p = hw_param_find(ctx->root, key);
        if (p && p->value[0]) ret = p->value;
    }
    /* 借用指针：调用方须自行保证不与 hw_param_set_value 并发改写同一 key */
    return ret;
}

int hw_param_get_int(hw_param_context_t *ctx, const char *key, int def) {
    if (!ctx || !ctx->root || !key) return def;
    char buf[256] = "";
    HW_RDLOCK_GUARD(&ctx->lock) {
        hw_param_t *p = hw_param_find(ctx->root, key);
        if (p && p->value[0]) snprintf(buf, sizeof(buf), "%s", p->value);
    }
    if (!buf[0]) return def;
    return (int)strtol(buf, NULL, 10);
}

bool hw_param_get_bool(hw_param_context_t *ctx, const char *key, bool def) {
    if (!ctx || !ctx->root || !key) return def;
    char buf[256] = "";
    HW_RDLOCK_GUARD(&ctx->lock) {
        hw_param_t *p = hw_param_find(ctx->root, key);
        if (p && p->value[0]) snprintf(buf, sizeof(buf), "%s", p->value);
    }
    if (!buf[0]) return def;
    if (hw_str_eq(buf, "true") || hw_str_eq(buf, "1") || hw_str_eq(buf, "yes")) return true;
    if (hw_str_eq(buf, "false") || hw_str_eq(buf, "0") || hw_str_eq(buf, "no")) return false;
    return def;
}

int hw_param_set_value(hw_param_context_t *ctx, const char *key, const char *value, int type,
                       const char *desc) {
    if (!ctx || !ctx->root) return HWRUN_EINVAL;
    int rc = HWRUN_OK;
    /* 锁内完成「读旧值 + 改树」，保证临界区原子；notify 移到出锁后 */
    char old_copy[256] = "";
    int has_old = 0;
    HW_WRLOCK_GUARD(&ctx->lock) {
        hw_param_t *p = hw_param_find(ctx->root, key);
        if (p && p->value[0]) {
            snprintf(old_copy, sizeof(old_copy), "%s", p->value);
            has_old = 1;
        }
        rc = hw_param_set(ctx->root, key, value, type, desc);
    }
    if (rc == HWRUN_OK) {
        hw_param_notify(ctx, key, has_old ? old_copy : NULL, value);
        hw_param_mark_revision(ctx, key);
    }
    return rc;
}

int hw_param_watch(hw_param_context_t *ctx, const char *plugin_id, const char *pattern,
                   int (*cb)(const char *, const char *, const char *, void *), void *userdata) {
    if (!ctx || !plugin_id || !cb) return HWRUN_EINVAL;
    int rc = HWRUN_OK;
    HW_WRLOCK_GUARD(&ctx->lock) {
        hw_param_watcher_t *w = calloc(1, sizeof(*w));
        if (!w) {
            rc = HWRUN_ENOMEM;
        } else {
            snprintf(w->plugin_id, sizeof(w->plugin_id), "%s", plugin_id);
            if (pattern) snprintf(w->pattern, sizeof(w->pattern), "%s", pattern);
            w->on_change = cb;
            w->userdata = userdata;
            w->next = ctx->watchers;
            ctx->watchers = w;
        }
    }
    return rc;
}

void hw_param_notify(hw_param_context_t *ctx, const char *key, const char *old_v,
                     const char *new_v) {
    if (!ctx || !key) return;
    /* 锁内快照匹配 watcher（cb+userdata；watcher 可能被并发释放），
     * 出锁后逐个派发 —— 持锁期间绝不调用 watcher 回调。
     * 快照上限 64：超限静默截断（watcher 数百的场景不存在）。 */
    typedef int (*watch_cb_t)(const char *, const char *, const char *, void *);
    struct {
        watch_cb_t cb;
        void *ud;
    } snaps[64];
    int n = 0;
    HW_RDLOCK_GUARD(&ctx->lock) {
        for (hw_param_watcher_t *w = ctx->watchers; w; w = w->next) {
            if (w->pattern[0] && fnmatch(w->pattern, key, 0) != 0) continue;
            if (n < (int)(sizeof(snaps) / sizeof(snaps[0]))) {
                snaps[n].cb = w->on_change;
                snaps[n].ud = w->userdata;
                n++;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        if (snaps[i].cb) snaps[i].cb(key, old_v, new_v, snaps[i].ud);
    }
}

void hw_param_mark_revision(hw_param_context_t *ctx, const char *reason) {
    if (!ctx || !ctx->initialized) return;
    /* set_value 已在此前的写锁外派发（notify 之后调用本函数），锁不在此处持有；
     * 直接把"参数树被改动"事件转发给 bus 装配的钩子（GIT 自动 commit 等）。
     * param 属第 1 环，不在此 include 任何 GIT 头。 */
    if (ctx->on_revision) ctx->on_revision(ctx->revision_userdata, reason);
}

int hw_param_load_file(hw_param_context_t *ctx, const char *path, int source) {
    if (!ctx || !path) return HWRUN_EINVAL;
    FILE *fp = fopen(path, "r");
    if (!fp) return HWRUN_ENOENT;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p || *p == '#' || *p == '\n' || *p == '\r') continue;
        if (*p == ' ') continue; /* 缩进(嵌套)行：扁平解析跳过 */

        char *sep = strchr(p, ':');
        if (!sep) sep = strchr(p, '=');
        if (!sep) continue;
        *sep = '\0';
        char *key = p;
        char *val = sep + 1;

        char *ke = key + strlen(key);
        while (ke > key && isspace((unsigned char)ke[-1]))
            ke--;
        *ke = '\0';

        while (*val == ' ' || *val == '\t' || *val == '\"')
            val++;
        char *ve = val + strlen(val);
        while (ve > val && (isspace((unsigned char)ve[-1]) || ve[-1] == '\"' || ve[-1] == '\r' ||
                            ve[-1] == '\n'))
            ve--;
        *ve = '\0';

        if (!*key) continue;
        hw_param_set(ctx->root, key, val, HWPARAM_TYPE_STRING, NULL);
    }
    fclose(fp);
    (void)source;
    return HWRUN_OK;
}

int hw_param_save_file(hw_param_context_t *ctx, const char *path) {
    if (!ctx || !ctx->root || !path) return HWRUN_EINVAL;
    FILE *fp = fopen(path, "w");
    if (!fp) return HWRUN_ENOENT;

    /* 递归写出，帮助函数收集前缀 */
    char prefix[512] = "";
    int rc = hw_param_tree_save(ctx->root, prefix, fp);
    fclose(fp);
    return rc;
}

/* 递归把参数树写成扁平 "a.b.c: value" */
static int hw_param_tree_save(const hw_param_t *p, const char *prefix, FILE *fp) {
    if (!p) return HWRUN_OK;
    for (const hw_param_t *c = p->child; c; c = c->sibling) {
        char kk[512];
        if (prefix[0])
            snprintf(kk, sizeof(kk), "%s.%s", prefix, c->key);
        else
            snprintf(kk, sizeof(kk), "%s", c->key);
        if (c->value[0]) {
            fprintf(fp, "%s: %s\n", kk, c->value);
        }
        hw_param_tree_save(c, kk, fp);
    }
    return HWRUN_OK;
}

void hw_param_dump_all(hw_param_context_t *ctx) {
    if (!ctx || !ctx->root || !ctx->root->child) return;
    extern void hw_param_dump(hw_param_t * p, int indent);
    hw_param_dump(ctx->root->child, 0);
}