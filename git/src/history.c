/*
 * history.c — Git 历史查询
 *
 * log / blame / diff / status 等只读历史查询操作。
 * 结果以链表形式返回，调用方用 free_commits()/free_blame() 释放。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "git_internal.h"

#define PLUGIN_ID "git"

/* ============================================================
   查看提交日志
   ============================================================ */
git_commit_t *git_ops_log(int limit, const char *since) {
    char cmd[640] = "log --pretty=format:'%h|%an|%ai|%s'";

    if (limit > 0) {
        int n = (int)strlen(cmd);
        snprintf(cmd + n, sizeof(cmd) - n, " -n %d", limit);
    }
    if (since && since[0]) {
        int n = (int)strlen(cmd);
        /* 允许 "2026-01-01" 或 "1.week.ago" 等形式 */
        snprintf(cmd + n, sizeof(cmd) - n, " --since=%s", since);
    }

    git_result_t *r = git_exec(NULL, cmd);
    if (!r || !r->success) {
        if (r) git_result_free(r);
        return NULL;
    }

    git_commit_t *head = NULL, *tail = NULL;
    char *line = r->stdout_buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        git_commit_t *c = (git_commit_t *)calloc(1, sizeof(git_commit_t));
        if (c) {
            char *p1 = strchr(line, '|');
            if (p1) {
                *p1 = '\0';
                snprintf(c->hash, sizeof(c->hash), "%s", line);
                line = p1 + 1;
                p1 = strchr(line, '|');
                if (p1) {
                    *p1 = '\0';
                    snprintf(c->author, sizeof(c->author), "%s", line);
                    line = p1 + 1;
                    p1 = strchr(line, '|');
                    if (p1) {
                        *p1 = '\0';
                        snprintf(c->date, sizeof(c->date), "%s", line);
                        snprintf(c->message, sizeof(c->message), "%s", p1 + 1);
                    } else {
                        snprintf(c->date, sizeof(c->date), "%s", line);
                    }
                }
            }
            if (tail) { tail->next = c; tail = c; }
            else      { head = c;       tail = c; }
        }

        line = nl ? nl + 1 : NULL;
    }

    git_result_free(r);
    return head;
}

void git_ops_free_commits(git_commit_t *head) {
    while (head) {
        git_commit_t *n = head->next;
        free(head);
        head = n;
    }
}

/* ============================================================
   Blame 文件溯源
   ============================================================ */
git_blame_t *git_ops_blame(const char *file) {
    if (!file) return NULL;
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "blame --line-porcelain -c -- %s", file);

    git_result_t *r = git_exec(NULL, cmd);
    if (!r || !r->success) {
        if (r) git_result_free(r);
        return NULL;
    }

    git_blame_t *head = NULL, *tail = NULL;
    uint32_t line_no = 0;
    char cur_hash[64] = "", cur_author[128] = "", cur_date[32] = "";
    char *line = r->stdout_buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        /* 形如 <hash> <orig_line> <final_line> <num> */
        if ((line[0] >= '0' && line[0] <= '9') ||
            (line[0] >= 'a' && line[0] <= 'f')) {
            char *sp = strchr(line, ' ');
            if (sp) {
                size_t hlen = (size_t)(sp - line);
                if (hlen > sizeof(cur_hash) - 1)
                    hlen = sizeof(cur_hash) - 1;
                memcpy(cur_hash, line, hlen);
                cur_hash[hlen] = '\0';
                line_no++;
            }
        } else if (!strncmp(line, "author ", 7)) {
            snprintf(cur_author, sizeof(cur_author), "%s", line + 7);
        } else if (!strncmp(line, "author-time ", 12)) {
            /* 转换为 YYYY-MM-DD */
            long t = atol(line + 12);
            char *d = ctime(&t);
            if (d) {
                int n = (int)strlen(d);
                if (n > 0 && d[n - 1] == '\n') d[n - 1] = '\0';
                snprintf(cur_date, sizeof(cur_date), "%s", d);
            }
        } else if (!strncmp(line, "filename ", 9) ||
                   !strncmp(line, "summary ", 8) ||
                   !strncmp(line, "\t", 1)) {
            if (line[0] == '\t') {
                /* 实际内容行：汇出一条 blame 记录 */
                git_blame_t *b = (git_blame_t *)calloc(1, sizeof(git_blame_t));
                if (b) {
                    b->line_no = line_no;
                    snprintf(b->commit_id, sizeof(b->commit_id), "%s", cur_hash);
                    snprintf(b->author,   sizeof(b->author),   "%s", cur_author);
                    snprintf(b->date,     sizeof(b->date),     "%s", cur_date);
                    snprintf(b->content,  sizeof(b->content),  "%s", line + 1);
                    if (tail) { tail->next = b; tail = b; }
                    else      { head = b;       tail = b; }
                }
                cur_hash[0] = cur_author[0] = cur_date[0] = '\0';
            }
        }

        line = nl ? nl + 1 : NULL;
    }

    git_result_free(r);
    return head;
}

void git_ops_free_blame(git_blame_t *head) {
    while (head) {
        git_blame_t *n = head->next;
        free(head);
        head = n;
    }
}

/* ============================================================
   Diff 差异（返回 malloc 字符串，调用方 free）
   ============================================================ */
char *git_ops_diff(const char *c1, const char *c2) {
    char cmd[640];
    if (c1 && c2 && c1[0] && c2[0])
        snprintf(cmd, sizeof(cmd), "diff %s %s", c1, c2);
    else if (c1 && c1[0])
        snprintf(cmd, sizeof(cmd), "diff %s", c1);
    else
        snprintf(cmd, sizeof(cmd), "diff");

    git_result_t *r = git_exec(NULL, cmd);
    if (!r) {
        return NULL;
    }
    if (!r->success) {
        git_result_free(r);
        return NULL;
    }
    char *out = r->stdout_buf ? strdup(r->stdout_buf) : NULL;
    git_result_free(r);
    return out;
}

/* ============================================================
   Status 工作区状态
   ============================================================ */
char *git_ops_status(void) {
    git_result_t *r = git_exec(NULL, "status --short");
    if (!r) return NULL;
    if (!r->success) {
        git_result_free(r);
        return NULL;
    }
    char *out = r->stdout_buf ? strdup(r->stdout_buf) : NULL;
    git_result_free(r);
    return out;
}