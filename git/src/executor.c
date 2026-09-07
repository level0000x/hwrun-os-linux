/*
 * executor.c — Git 命令执行器（核心封装）
 *
 * 统一调用系统 /usr/bin/git 命令（popen），负责：
 *   1. 拼装命令并在目标目录执行
 *   2. 捕获标准输出/错误
 *   3. 记录审计日志
 *
 * 本模块不重新实现 Git，全部委托给系统 git。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "git_internal.h"

#define PLUGIN_ID  "git"
#define GIT_BIN    "git"
#define GIT_CONF   "git.conf"

/* 全局上下文单例 */
git_context_t git_g_ctx;

/* ============================================================
   Git 命令执行器
   ============================================================ */
git_result_t *git_exec(const char *cwd, const char *cmd) {
    if (!cmd) return NULL;
    git_result_t *r = (git_result_t *)calloc(1, sizeof(git_result_t));
    if (!r) return NULL;

    const char *work = cwd ? cwd : git_g_ctx.repo_path;

    /* 命令 = cd <dir> && git <cmd> 2>&1 */
    char full[4096];
    snprintf(full, sizeof(full), "cd \"%s\" 2>/dev/null && %s %s 2>&1",
             work, GIT_BIN, cmd);

    FILE *fp = popen(full, "r");
    if (!fp) {
        snprintf(r->error, sizeof(r->error), "popen 失败: 无法启动 git");
        r->success = 0;
        return r;
    }

    /* 逐步读取输出 */
    size_t cap = 8192, len = 0;
    r->stdout_buf = (char *)malloc(cap);
    if (r->stdout_buf) {
        char buf[1024];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
            if (len + n + 1 > cap) {
                cap = (len + n + 1) * 2;
                char *nb = (char *)realloc(r->stdout_buf, cap);
                if (!nb) break;
                r->stdout_buf = nb;
            }
            memcpy(r->stdout_buf + len, buf, n);
            len += n;
        }
        r->stdout_buf[len] = '\0';
        r->stdout_len = len;
    }

    int exit_code = pclose(fp);
    r->exit_code = exit_code;
    r->success = (exit_code == 0);
    if (!r->success) {
        snprintf(r->error, sizeof(r->error),
                 "git 命令失败, exit_code=%d", exit_code);
    }
    return r;
}

void git_result_free(git_result_t *r) {
    if (!r) return;
    free(r->stdout_buf);
    free(r->stderr_buf);
    free(r);
}

git_result_t *git_exec_audit(const char *cwd, const char *cmd,
                             const char *user, const char *operation,
                             const char *target) {
    /* 操作前记录 */
    git_audit_write(&git_g_ctx, user, operation, target, cmd, 0);
    git_result_t *r = git_exec(cwd, cmd);
    /* 操作结果记录 */
    git_audit_write(&git_g_ctx, user, operation, target, cmd,
                    r ? r->success : 0);
    return r;
}

/* ============================================================
   审计日志
   ============================================================ */
void git_audit_write(git_context_t *ctx, const char *user,
                     const char *operation, const char *target,
                     const char *message, int success) {
    if (!ctx) return;
    struct stat st;
    /* 确保审计目录存在 */
    if (stat(ctx->audit_path, &st) != 0) {
        char mk[512];
        snprintf(mk, sizeof(mk), "mkdir -p \"%s\"", ctx->audit_path);
        (void)system(mk);
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/audit.log", ctx->audit_path);

    time_t now = time(NULL);
    char line[1024];
    snprintf(line, sizeof(line),
             "{\"ts\":%ld,\"user\":\"%s\",\"op\":\"%s\",\"target\":\"%s\","
             "\"msg\":\"%s\",\"result\":\"%s\"}\n",
             (long)now,
             user      ? user      : "system",
             operation ? operation : "unknown",
             target    ? target    : "",
             message   ? message   : "",
             success   ? "success" : "failed");

    FILE *fp = fopen(path, "a");
    if (fp) {
        fputs(line, fp);
        fclose(fp);
    }
}

/* ============================================================
   配置加载
   ============================================================ */
int git_config_load(git_context_t *ctx) {
    if (!ctx) return HWRUN_EINVAL;
    git_config_t *cfg = &ctx->config;

    /* 内置默认值 */
    snprintf(cfg->repo_path,  sizeof(cfg->repo_path),  "%s", "/var/lib/hwrun/git");
    snprintf(cfg->audit_path, sizeof(cfg->audit_path), "%s", "/var/lib/hwrun/git-audit");
    snprintf(cfg->branch,     sizeof(cfg->branch),     "%s", "main");
    cfg->remote_url[0] = '\0';
    cfg->auto_commit  = 1;
    cfg->auto_push    = 0;
    cfg->auto_compact = 1;
    cfg->max_size     = 1024ULL * 1024ULL * 1024ULL;   /* 1GB */

    /* 允许通过环境变量覆盖状态目录 */
    char conf_path[512] = {0};
    const char *state = getenv("HWRUN_STATE");
    if (state) {
        snprintf(conf_path, sizeof(conf_path), "%s/%s", state, GIT_CONF);
    }

    if (conf_path[0] && access(conf_path, R_OK) == 0) {
        char text[4096];
        FILE *fp = fopen(conf_path, "r");
        if (fp) {
            while (fgets(text, sizeof(text), fp)) {
                char *key = text;
                char *eq = strchr(key, '=');
                if (!eq) continue;
                *eq = '\0';
                char *val = git_strtrim(eq + 1);
                git_strtrim(key);
                if (!strcmp(key, "repo_path"))
                    snprintf(cfg->repo_path, sizeof(cfg->repo_path), "%s", val);
                else if (!strcmp(key, "audit_path"))
                    snprintf(cfg->audit_path, sizeof(cfg->audit_path), "%s", val);
                else if (!strcmp(key, "remote"))
                    snprintf(cfg->remote_url, sizeof(cfg->remote_url), "%s", val);
                else if (!strcmp(key, "branch"))
                    snprintf(cfg->branch, sizeof(cfg->branch), "%s", val);
                else if (!strcmp(key, "auto_commit"))
                    cfg->auto_commit  = (!strcmp(val, "1")  || !strcmp(val, "true"));
                else if (!strcmp(key, "auto_push"))
                    cfg->auto_push    = (!strcmp(val, "1")  || !strcmp(val, "true"));
                else if (!strcmp(key, "auto_compact"))
                    cfg->auto_compact = (!strcmp(val, "1")  || !strcmp(val, "true"));
                else if (!strcmp(key, "max_size")) {
                    char *end = NULL;
                    unsigned long long v = strtoull(val, &end, 10);
                    if (end && (end[0] == 'G' || end[0] == 'g'))
                        v *= 1024ULL * 1024ULL * 1024ULL;
                    else if (end && (end[0] == 'M' || end[0] == 'm'))
                        v *= 1024ULL * 1024ULL;
                    cfg->max_size = v;
                }
            }
            fclose(fp);
        }
    }

    /* 同步上下文 */
    snprintf(ctx->repo_path,  sizeof(ctx->repo_path),  "%s", cfg->repo_path);
    snprintf(ctx->audit_path, sizeof(ctx->audit_path), "%s", cfg->audit_path);
    return 0;
}

/* ============================================================
   文本工具
   ============================================================ */
char *git_strtrim(char *s) {
    if (!s) return NULL;
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    char *e = p + strlen(p);
    while (e > p && (e[-1] == ' ' || e[-1] == '\t' ||
                     e[-1] == '\r' || e[-1] == '\n')) e--;
    *e = '\0';
    return p;
}