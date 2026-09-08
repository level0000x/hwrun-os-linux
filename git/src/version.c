/*
 * version.c — Git 版本管理
 *
 * tag / checkout / reset / revert 等版本切换与打标操作。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "git_internal.h"

#define PLUGIN_ID "git"

/* 创建标签 */
int git_ops_tag(const char *name, const char *commit) {
    if (!name) return HWRUN_EINVAL;
    char cmd[512];
    if (commit && commit[0])
        snprintf(cmd, sizeof(cmd), "tag %s %s", name, commit);
    else
        snprintf(cmd, sizeof(cmd), "tag %s", name);
    git_result_t *r = git_exec_audit(NULL, cmd, "system", "tag", name);
    if (!r) return -1;
    int ret = r->success ? 0 : -1;
    git_result_free(r);
    return ret;
}

/* 检出（分支 / 标签 / commit） */
int git_ops_checkout(const char *ref) {
    if (!ref) return HWRUN_EINVAL;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "checkout %s", ref);
    git_result_t *r = git_exec_audit(NULL, cmd, "system", "checkout", ref);
    if (!r) return -1;
    int ret = r->success ? 0 : -1;
    if (!r->success)
        HWAPI_LOGE(PLUGIN_ID, "git checkout 失败: %s",
                   r->stdout_buf ? r->stdout_buf : "");
    git_result_free(r);
    return ret;
}

/* 重置（hard=1 → --hard, 否则 --soft） */
int git_ops_reset(const char *ref, int hard) {
    if (!ref) return HWRUN_EINVAL;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "reset %s %s", hard ? "--hard" : "--soft", ref);
    git_result_t *r = git_exec_audit(NULL, cmd, "system", "reset", ref);
    if (!r) return -1;
    int ret = r->success ? 0 : -1;
    if (!r->success)
        HWAPI_LOGE(PLUGIN_ID, "git reset 失败: %s",
                   r->stdout_buf ? r->stdout_buf : "");
    git_result_free(r);
    return ret;
}

/* 回滚指定 commit */
int git_ops_revert(const char *commit) {
    if (!commit) return HWRUN_EINVAL;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "revert %s --no-edit", commit);
    git_result_t *r = git_exec_audit(NULL, cmd, "system", "revert", commit);
    if (!r) return -1;
    int ret = r->success ? 0 : -1;
    if (!r->success)
        HWAPI_LOGE(PLUGIN_ID, "git revert 失败: %s",
                   r->stdout_buf ? r->stdout_buf : "");
    git_result_free(r);
    return ret;
}