/*
 * audit.c — Git 审计日志查询
 *
 * 反向读取审计仓库 audit.log（JSON Lines），按 limit / user / since
 * 过滤为 git_audit_entry_t 链表。调用方用 free_audit() 释放。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "git_internal.h"

#define PLUGIN_ID "git"

/* 简单提取 JSON 字段值 */
static void extract_field(const char *text, const char *key, char *out, size_t cap) {
    size_t klen = strlen(key);
    const char *p = text;
    while ((p = strstr(p, key)) != NULL) {
        /* 匹配 "key": 前缀，避免误配子串 */
        if ((p == text || p[-1] == '{' || p[-1] == ',' || p[-1] == '[') && p[klen] == '"' &&
            p[klen + 1] == ':') {
            const char *v = p + klen + 2;
            const char *q = v;
            while (*q && *q != '"')
                q++;
            size_t n = (size_t)(q - v);
            if (n >= cap) n = cap - 1;
            memcpy(out, v, n);
            out[n] = '\0';
            return;
        }
        p += klen;
    }
    out[0] = '\0';
}

/* 释放审计链表 */
void git_ops_free_audit(git_audit_entry_t *head) {
    while (head) {
        git_audit_entry_t *n = head->next;
        free(head);
        head = n;
    }
}

git_audit_entry_t *git_ops_audit(int limit, const char *user, uint64_t since) {
    char path[512];
    snprintf(path, sizeof(path), "%s/audit.log", git_g_ctx.audit_path);

    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    /* 先统计行数（JSON Lines：一行一条） */
    int total = 0;
    char ch;
    int prev_nl = 1;
    while ((ch = (char)fgetc(fp)) != EOF) {
        if (ch == '\n') {
            total++;
            prev_nl = 1;
        } else
            prev_nl = 0;
    }
    if (!prev_nl) total++;
    if (limit > 0 && total > limit) {
        long skip = 0;
        /* 快进跳过 limit 条，取最近 limit 条 */
        rewind(fp);
        int skipped = 0;
        if (fseek(fp, 0, SEEK_SET) == 0) {
            int c;
            int in_skip = 1;
            while (in_skip && (c = fgetc(fp)) != EOF) {
                if (c == '\n') {
                    skipped++;
                    if (skipped >= total - limit) in_skip = 0;
                }
            }
        }
        skip = ftell(fp);
        (void)skip;
    } else {
        rewind(fp);
    }

    git_audit_entry_t *head = NULL, *tail = NULL;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        git_audit_entry_t *e = (git_audit_entry_t *)calloc(1, sizeof(*e));
        if (!e) break;

        char tmp[256];
        extract_field(line, "ts", tmp, sizeof(tmp));
        e->timestamp = strtoull(tmp, NULL, 10);
        extract_field(line, "user", e->user, sizeof(e->user));
        extract_field(line, "op", e->operation, sizeof(e->operation));
        extract_field(line, "target", e->target, sizeof(e->target));
        extract_field(line, "msg", e->message, sizeof(e->message));
        extract_field(line, "result", e->result, sizeof(e->result));

        /* 过滤 */
        if ((user && user[0] && strcmp(e->user, user) != 0) || (since && e->timestamp < since)) {
            free(e);
            continue;
        }

        if (tail) {
            tail->next = e;
            tail = e;
        } else {
            head = e;
            tail = e;
        }
    }
    fclose(fp);
    return head;
}