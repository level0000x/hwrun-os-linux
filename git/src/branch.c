/*
 * branch.c — Git 分支管理
 *
 * branch（创建分支）/ merge（合并分支）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "git_internal.h"

#define PLUGIN_ID "git"

/* 创建分支（base 可选，缺省基于当前 HEAD） */
int git_ops_branch(const char *name, const char *base) {
    if (!name) return HWRUN_EINVAL;
    char cmd[512];
    if (base && base[0])
        snprintf(cmd, sizeof(cmd), "branch %s %s", name, base);
    else
        snprintf(cmd, sizeof(cmd), "branch %s", name);
    git_result_t *r = git_exec_audit(NULL, cmd, "system", "branch", name);
    if (!r) return -1;
    int ret = r->success ? 0 : -1;
    git_result_free(r);
    return ret;
}

/* 合并分支 */
int git_ops_merge(const char *branch, const char *message) {
    if (!branch) return HWRUN_EINVAL;
    char cmd[512];
    if (message && message[0])
        snprintf(cmd, sizeof(cmd), "merge %s -m \"%s\"", branch, message);
    else
        snprintf(cmd, sizeof(cmd), "merge %s --no-edit", branch);
    git_result_t *r = git_exec_audit(NULL, cmd, "system", "merge", branch);
    if (!r) return -1;
    int ret = r->success ? 0 : -1;
    if (!r->success)
        HWAPI_LOGE(PLUGIN_ID, "git merge 失败: %s",
                   r->stdout_buf ? r->stdout_buf : "");
    git_result_free(r);
    return ret;
}