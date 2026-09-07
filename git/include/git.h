/*
 * git.h — GIT 协议核心数据结构与对外接口
 *
 * HWRun OS 的 GIT 插件对外暴露的公共类型与 ops 接口。
 * GIT 是系统的"时间机器"：一切状态皆可追溯、可回滚。
 *
 * 设计哲学：薄封装。本插件不重新实现 Git，所有实际操作由
 * 系统 /usr/bin/git 命令完成，额外提供统一入口、审计日志
 * 与历史压缩管理。
 */

#ifndef HWRUN_GIT_H
#define HWRUN_GIT_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
   Git 操作类型
   ============================================================ */
typedef enum {
    GIT_OP_INIT,
    GIT_OP_CLONE,
    GIT_OP_ADD,
    GIT_OP_COMMIT,
    GIT_OP_PUSH,
    GIT_OP_PULL,
    GIT_OP_TAG,
    GIT_OP_CHECKOUT,
    GIT_OP_RESET,
    GIT_OP_REVERT,
    GIT_OP_LOG,
    GIT_OP_BLAME,
    GIT_OP_DIFF,
    GIT_OP_BRANCH,
    GIT_OP_MERGE,
    GIT_OP_COMPACT,
    GIT_OP_SIZE,
    GIT_OP_STATUS,
    GIT_OP_AUDIT,
} git_op_type_t;

/* ============================================================
   Git 命令执行结果
   ============================================================ */
typedef struct git_result {
    int    success;                 /* 命令是否成功            */
    int    exit_code;               /* git 命令退出码          */
    char  *stdout_buf;              /* 标准输出（含 stderr）   */
    size_t stdout_len;
    char  *stderr_buf;
    size_t stderr_len;
    char   error[256];              /* 错误描述                */
} git_result_t;

/* ============================================================
   Git 配置
   ============================================================ */
typedef struct git_config {
    char repo_path[256];            /* 主仓库路径              */
    char audit_path[256];           /* 审计仓库路径            */
    char remote_url[256];           /* 远程仓库地址            */
    char branch[64];                /* 当前分支                */
    int  auto_commit;               /* 是否自动 commit         */
    int  auto_push;                 /* 是否自动 push           */

    /* 历史管理 */
    uint64_t max_size;              /* 最大仓库大小（字节）    */
    int      auto_compact;          /* 是否自动压缩            */
} git_config_t;

/* ============================================================
   Commit 信息（log 查询结果节点）
   ============================================================ */
typedef struct git_commit {
    char hash[64];
    char author[128];
    char date[32];
    char message[256];
    struct git_commit *next;
} git_commit_t;

/* ============================================================
   Blame 行信息（blame 查询结果节点）
   ============================================================ */
typedef struct git_blame {
    uint32_t line_no;
    char     commit_id[64];
    char     author[128];
    char     date[32];
    char     content[512];
    struct git_blame *next;
} git_blame_t;

/* ============================================================
   审计记录
   ============================================================ */
typedef struct git_audit_entry {
    uint64_t timestamp;
    char user[64];
    char operation[32];
    char target[256];
    char message[256];
    char result[16];
    struct git_audit_entry *next;
} git_audit_entry_t;

/* ============================================================
   Git 上下文
   ============================================================ */
typedef struct git_context {
    git_config_t config;
    char repo_path[256];
    char audit_path[256];
    int  initialized;
    uint64_t git_task_id;           /* GIT 自身的任务 ID       */
    void *lock;
} git_context_t;

/* ============================================================
   GIT 协议接口（get_interface("GIT") 返回本结构体）
   ============================================================ */
typedef struct hw_git_ops {
    /* === 存储管理 === */
    int  (*init)   (const char *path);                    /* 初始化仓库      */
    int  (*clone)  (const char *remote, const char *local_path); /* 克隆   */
    int  (*add)    (const char *path);                    /* 添加文件        */
    int  (*commit) (const char *message, char *out_id, size_t cap); /* 提交 */
    int  (*push)   (const char *remote, const char *branch);        /* 推送 */
    int  (*pull)   (const char *remote, const char *branch);        /* 拉取 */

    /* === 版本管理 === */
    int  (*tag)      (const char *name, const char *commit);      /* 打标签 */
    int  (*checkout) (const char *ref);                            /* 检出   */
    int  (*reset)    (const char *ref, int hard);                  /* 重置   */
    int  (*revert)   (const char *commit);                         /* 回滚   */

    /* === 历史查询 === */
    git_commit_t *(*log)   (int limit, const char *since);  /* 提交日志       */
    git_blame_t  *(*blame) (const char *file);              /* 文件溯源       */
    char         *(*diff)  (const char *c1, const char *c2); /* 差异         */

    /* === 分支管理 === */
    int  (*branch) (const char *name, const char *base);           /* 建分支 */
    int  (*merge)  (const char *branch, const char *message);      /* 合并   */

    /* === 历史管理 === */
    uint64_t (*size)   (void);                            /* 仓库大小         */
    int      (*compact)(int level, uint64_t *old_size, uint64_t *new_size); /* 压缩 */
    char     *(*status)(void);                            /* 工作区状态       */

    /* === 审计 === */
    git_audit_entry_t *(*audit)(int limit, const char *user, uint64_t since);

    /* === 内存释放 === */
    void (*free_commits)(git_commit_t *head);
    void (*free_blame)  (git_blame_t *head);
    void (*free_audit)  (git_audit_entry_t *head);

    /* === 配置 === */
    git_config_t *(*get_config)(void);
} hw_git_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_GIT_H */