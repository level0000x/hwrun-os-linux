/*
 * storage.c — Git 存储管理
 *
 * init / clone / add / commit / push / pull 等仓库存储层操作。
 * 全部委托系统 git 命令执行并记录审计日志。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "git_internal.h"

#define PLUGIN_ID "git"

/* 确保目录存在 */
static void ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        char mk[512];
        snprintf(mk, sizeof(mk), "mkdir -p \"%s\"", path);
        (void)system(mk);
    }
}

/* 初始化仓库
 * path 为 NULL 时初始化主仓库；否则在给定路径初始化。 */
int git_ops_init(const char *path) {
    const char *dir;
    char tmp_cwd[512] = {0};

    if (path && path[0]) {
        /* 给定路径：确保目录存在后在内部 git init */
        ensure_dir(path);
        snprintf(tmp_cwd, sizeof(tmp_cwd), "%s", path);
        dir = tmp_cwd;
    } else {
        /* 主仓库 */
        ensure_dir(git_g_ctx.repo_path);
        dir = git_g_ctx.repo_path;
    }

    git_result_t *r = git_exec_audit(dir, "init", "system", "init",
                                     path ? path : git_g_ctx.repo_path);
    if (!r) return -1;
    int ret = r->success ? 0 : -1;
    if (!r->success)
        HWAPI_LOGE(PLUGIN_ID, "git init 失败: %s",
                   r->stdout_buf ? r->stdout_buf : "");
    git_result_free(r);
    return ret;
}

/* 克隆仓库 */
int git_ops_clone(const char *remote, const char *local_path) {
    if (!remote || !local_path) return HWRUN_EINVAL;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "clone %s \"%s\"", remote, local_path);
    git_result_t *r = git_exec_audit(NULL, cmd, "system", "clone", remote);
    if (!r) return -1;
    int ret = r->success ? 0 : -1;
    if (!r->success)
        HWAPI_LOGE(PLUGIN_ID, "git clone 失败: %s",
                   r->stdout_buf ? r->stdout_buf : "");
    git_result_free(r);
    return ret;
}

/* 添加文件（可多次 add 空格分隔路径） */
int git_ops_add(const char *path) {
    if (!path) return HWRUN_EINVAL;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "add -- %s", path);
    git_result_t *r = git_exec_audit(NULL, cmd, "system", "add", path);
    if (!r) return -1;
    int ret = r->success ? 0 : -1;
    git_result_free(r);
    return ret;
}

/* 提交（返回 commit_id 到 out_id） */
int git_ops_commit(const char *message, char *out_id, size_t cap) {
    if (!message) return HWRUN_EINVAL;
    char cmd[1200];
    /* 转义引号 */
    char msg[512];
    size_t j = 0;
    for (size_t i = 0; message[i] && j < sizeof(msg) - 2; i++) {
        if (message[i] == '"' || message[i] == '\\') msg[j++] = '\\';
        msg[j++] = message[i];
    }
    msg[j] = '\0';
    snprintf(cmd, sizeof(cmd), "commit -m \"%s\"", msg);

    git_result_t *r = git_exec_audit(NULL, cmd, "system", "commit", message);
    if (!r) return -1;
    int ret = r->success ? 0 : -1;

    /* 成功则取 commit hash */
    if (ret == 0 && out_id && cap > 0) {
        git_result_t *lr = git_exec(NULL, "rev-parse HEAD");
        if (lr && lr->success && lr->stdout_buf) {
            char *p = strchr(lr->stdout_buf, '\n');
            if (p) *p = '\0';
            snprintf(out_id, cap, "%s", lr->stdout_buf);
        }
        if (lr) git_result_free(lr);
    }
    git_result_free(r);
    return ret;
}

/* 推送 */
int git_ops_push(const char *remote, const char *branch) {
    char cmd[512] = "push";
    if (remote && remote[0]) {
        snprintf(cmd, sizeof(cmd), "push %s", remote);
        if (branch && branch[0]) {
            size_t n = strlen(cmd);
            snprintf(cmd + n, sizeof(cmd) - n, " %s", branch);
        }
    }
    git_result_t *r = git_exec_audit(NULL, cmd, "system", "push",
                                     remote ? remote : git_g_ctx.config.remote_url);
    if (!r) return -1;
    int ret = r->success ? 0 : -1;
    if (!r->success)
        HWAPI_LOGE(PLUGIN_ID, "git push 失败: %s",
                   r->stdout_buf ? r->stdout_buf : "");
    git_result_free(r);
    return ret;
}

/* 拉取 */
int git_ops_pull(const char *remote, const char *branch) {
    char cmd[512] = "pull";
    if (remote && remote[0]) {
        snprintf(cmd, sizeof(cmd), "pull %s", remote);
        if (branch && branch[0]) {
            size_t n = strlen(cmd);
            snprintf(cmd + n, sizeof(cmd) - n, " %s", branch);
        }
    }
    git_result_t *r = git_exec_audit(NULL, cmd, "system", "pull",
                                     remote ? remote : git_g_ctx.config.remote_url);
    if (!r) return -1;
    int ret = r->success ? 0 : -1;
    if (!r->success)
        HWAPI_LOGE(PLUGIN_ID, "git pull 失败: %s",
                   r->stdout_buf ? r->stdout_buf : "");
    git_result_free(r);
    return ret;
}