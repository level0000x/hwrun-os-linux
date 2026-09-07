/*
 * git_internal.h — GIT 插件内部共享声明
 *
 * executor / 配置加载 / 审计日志 等仅在本插件内部使用的接口，
 * 不对外暴露，供各子模块(src 下各 .c)共享。
 */

#ifndef HWRUN_GIT_INTERNAL_H
#define HWRUN_GIT_INTERNAL_H

#include "hwrun.h"
#include "git.h"

/* ============================================================
   Git 命令执行器
   ============================================================ */
/* 执行 Git 命令：在 cwd（NULL=主仓库）目录下运行 git <cmd>。
 * 结果写入 res, stdout_buf/stderr_buf 为 malloc 分配，调用方
 * 用 git_result_free() 释放。 */
extern git_result_t *git_exec(const char *cwd, const char *cmd);
extern void          git_result_free(git_result_t *r);

/* 带审计日志地执行 Git 命令（先记录请求、后记录结果） */
extern git_result_t *git_exec_audit(const char *cwd, const char *cmd,
                                    const char *user, const char *operation,
                                    const char *target);

/* ============================================================
   Git 上下文（全局单例，各子模块共享）
   ============================================================ */
extern git_context_t git_g_ctx;

/* ============================================================
   配置加载
   ============================================================ */
/* 加载配置：优先读 $HWRUN_STATE/git.conf（key=value），
 * 否则使用内置默认值。返回 0 成功。 */
extern int git_config_load(git_context_t *ctx);

/* ============================================================
   审计日志
   ============================================================ */
/* 追加一条审计记录到审计仓库 audit.log（JSON Lines 格式） */
extern void git_audit_write(git_context_t *ctx, const char *user,
                            const char *operation, const char *target,
                            const char *message, int success);

/* ============================================================
   文本工具
   ============================================================ */
extern char *git_strtrim(char *s);

#endif /* HWRUN_GIT_INTERNAL_H */