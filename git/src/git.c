/*
 * git.c — GIT 插件入口与生命周期
 *
 * 导出 hw_plugin_entry()，返回填充好的 hw_plugin_t*。
 * ops.get_interface("GIT") 返回 hw_git_ops_t 协议接口。
 *
 * 依赖链：METAPROTO → LOG / PARAM → GIT（本插件的依赖声明见 plugin.yml）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "hwrun.h"
#include "hwrun_plugin.h"
#include "git.h"
#include "git_internal.h"

#define PLUGIN_ID  "git"
#define GIT_PROTO  "GIT"

/* 子模块实现（见 src 下各 .c 文件） */
/* storage.c */
extern int git_ops_init(const char *path);
extern int git_ops_clone(const char *remote, const char *local_path);
extern int git_ops_add(const char *path);
extern int git_ops_commit(const char *message, char *out_id, size_t cap);
extern int git_ops_push(const char *remote, const char *branch);
extern int git_ops_pull(const char *remote, const char *branch);
/* version.c */
extern int git_ops_tag(const char *name, const char *commit);
extern int git_ops_checkout(const char *ref);
extern int git_ops_reset(const char *ref, int hard);
extern int git_ops_revert(const char *commit);
/* history.c */
extern git_commit_t *git_ops_log(int limit, const char *since);
extern git_blame_t  *git_ops_blame(const char *file);
extern char         *git_ops_diff(const char *c1, const char *c2);
extern void          git_ops_free_commits(git_commit_t *head);
extern void          git_ops_free_blame(git_blame_t *head);
extern char         *git_ops_status(void);
/* branch.c */
extern int git_ops_branch(const char *name, const char *base);
extern int git_ops_merge(const char *branch, const char *message);
/* compact.c */
extern uint64_t git_ops_size(void);
extern int git_ops_compact(int level, uint64_t *old_size, uint64_t *new_size);
/* audit.c */
extern git_audit_entry_t *git_ops_audit(int limit, const char *user,
                                        uint64_t since);
extern void git_ops_free_audit(git_audit_entry_t *head);

/* 协议接口：返回当前配置（本文件实现） */
static git_config_t *git_ops_get_config(void);

/* ============================================================
   协议接口填充
   ============================================================ */
static hw_git_ops_t g_ops;

static void git_ops_init_table(void) {
    g_ops.init      = git_ops_init;
    g_ops.clone     = git_ops_clone;
    g_ops.add       = git_ops_add;
    g_ops.commit    = git_ops_commit;
    g_ops.push      = git_ops_push;
    g_ops.pull      = git_ops_pull;

    g_ops.tag       = git_ops_tag;
    g_ops.checkout  = git_ops_checkout;
    g_ops.reset     = git_ops_reset;
    g_ops.revert    = git_ops_revert;

    g_ops.log       = git_ops_log;
    g_ops.blame     = git_ops_blame;
    g_ops.diff      = git_ops_diff;

    g_ops.branch    = git_ops_branch;
    g_ops.merge     = git_ops_merge;

    g_ops.size      = git_ops_size;
    g_ops.compact   = git_ops_compact;
    g_ops.status    = git_ops_status;

    g_ops.audit     = git_ops_audit;

    g_ops.free_commits = git_ops_free_commits;
    g_ops.free_blame   = git_ops_free_blame;
    g_ops.free_audit   = git_ops_free_audit;

    g_ops.get_config   = git_ops_get_config;
}

/* 确保仓库存在（.git 目录），不存在则初始化 */
static void ensure_repo(void) {
    char gitdir[512];
    struct stat st;
    snprintf(gitdir, sizeof(gitdir), "%s/.git", git_g_ctx.repo_path);
    if (stat(gitdir, &st) != 0) {
        git_ops_init(git_g_ctx.repo_path);
    }
}

/* ============================================================
   生命周期回调
   ============================================================ */
static int git_plugin_init(hw_plugin_t *self) {
    (void)self;
    memset(&git_g_ctx, 0, sizeof(git_g_ctx));
    /* 加载配置（默认值 + $HWRUN_STATE/git.conf） */
    git_config_load(&git_g_ctx);
    /* 初始化协议接口表 */
    git_ops_init_table();
    /* 确保主仓库与审计目录存在 */
    ensure_repo();
    git_audit_write(&git_g_ctx, "system", "init", git_g_ctx.repo_path,
                    "GIT 插件初始化", 1);
    git_g_ctx.initialized = 1;
    HWAPI_LOGI("git", "init: repo=%s", git_g_ctx.repo_path);
    return HWRUN_OK;
}

static int git_plugin_start(hw_plugin_t *self) {
    (void)self;
    return HWRUN_OK;
}

static int git_plugin_stop(hw_plugin_t *self) {
    (void)self;
    if (git_g_ctx.initialized) {
        git_audit_write(&git_g_ctx, "system", "stop", git_g_ctx.repo_path,
                        "GIT 插件停止", 1);
        git_g_ctx.initialized = 0;
    }
    return HWRUN_OK;
}

static int git_plugin_destroy(hw_plugin_t *self) {
    (void)self;
    return HWRUN_OK;
}

/* 取得对外提供的协议接口实现 */
static void *git_plugin_get_interface(const char *protocol) {
    if (protocol && strcmp(protocol, GIT_PROTO) == 0) {
        return &g_ops;
    }
    return NULL;
}

/* 返回当前 Git 配置 */
static git_config_t *git_ops_get_config(void) {
    return &git_g_ctx.config;
}

/* ============================================================
   插件生命周期回调表（hw_plugin_ops_t，SDK 宏将其拷贝进描述符）
   ============================================================ */
static hw_plugin_ops_t g_ops_desc = {
    .init          = git_plugin_init,
    .start         = git_plugin_start,
    .stop          = git_plugin_stop,
    .destroy       = git_plugin_destroy,
    .configure     = NULL,
    .get_interface = git_plugin_get_interface,
};

/* ============================================================
   运行时注入绑定点 + 插件描述符（统一由 hwrun_plugin.h 宏生成）
   ============================================================ */
HWRUN_PLUGIN_BIND()

/* 协议与依赖声明（plugin.yml 是对外的权威描述；.so 内自持一份） */
static const char *const g_provides[] = { GIT_PROTO, NULL };
static const char *const g_requires[] = {
    HWPROTO_LOG, HWPROTO_PARAM, HWPROTO_METAPROTO, NULL,
};

HWRUN_PLUGIN_DEFINE("git", "Git 版本控制", "1.0.0", HWPLUGIN_TYPE_GIT,
                    "系统 Git 命令的封装层 + 审计日志 + 自动历史管理",
                    &g_ops_desc, g_provides, g_requires)