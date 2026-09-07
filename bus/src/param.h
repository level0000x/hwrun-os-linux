/*
 * param.h — PARAM 参数系统接口
 *
 * 所有配置都是参数；参数驱动插件加载与行为。
 * 依赖链第 1 环。
 */

#ifndef HWRUN_PARAM_H
#define HWRUN_PARAM_H

#include "hwrun.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 参数来源层级 */
enum {
    HWPARAM_HW   = 0,   /* 硬件参数（硬件探测提供） */
    HWPARAM_SYS  = 1,   /* 系统参数（系统生成）     */
    HWPARAM_USER = 2,   /* 用户参数（用户配置）     */
    HWPARAM_PLUG = 3,   /* 插件参数（插件配置）     */
    HWPARAM_GIT  = 4,   /* Git 参数                */
    HWPARAM_FMT  = 5,   /* 格式参数                */
    HWPARAM_RUN  = 6,   /* 运行时参数              */
    HWPARAM_SRC_MAX,
};

/* 参数变更监听器 */
typedef struct hw_param_watcher {
    char plugin_id[64];
    int (*on_change)(const char *key, const char *old_value,
                     const char *new_value, void *userdata);
    void *userdata;
    char pattern[128];      /* 通配：""=全部, "scheduler.*" */
    struct hw_param_watcher *next;
} hw_param_watcher_t;

/* 参数上下文 */
typedef struct hw_param_context {
    hw_param_t *root;                     /* 参数树根 */
    hw_param_watcher_t *watchers;         /* 监听器   */
    char dirs[HWPARAM_SRC_MAX][256];      /* 各来源目录 */
    char git_state_file[256];             /* git 状态文件 */
    int  initialized;
} hw_param_context_t;

/* 初始化：默认加载 dirs 下全部 yml/conf 参数 */
extern int  hw_param_init(hw_param_context_t *ctx, const char *root_dir);
extern void hw_param_shutdown(hw_param_context_t *ctx);

/* 值读写 */
extern const char *hw_param_get(hw_param_context_t *ctx, const char *key);
extern int         hw_param_get_int(hw_param_context_t *ctx, const char *key,
                                    int def);
extern bool        hw_param_get_bool(hw_param_context_t *ctx, const char *key,
                                     bool def);
extern int         hw_param_set_value(hw_param_context_t *ctx, const char *key,
                                      const char *value, int type,
                                      const char *desc);

/* 监听 */
extern int  hw_param_watch(hw_param_context_t *ctx, const char *plugin_id,
                           const char *pattern,
                           int (*cb)(const char*,const char*,const char*,void*),
                           void *userdata);
extern void hw_param_notify(hw_param_context_t *ctx, const char *key,
                            const char *old_v, const char *new_v);

/* 变更自动 git commit（依赖 GIT 子系统，先标记待办） */
extern void hw_param_mark_revision(hw_param_context_t *ctx,
                                   const char *reason);

/* 加载/保存到文件（简单键值 / 近似 yml） */
extern int  hw_param_load_file(hw_param_context_t *ctx, const char *path,
                               int source);
extern int  hw_param_save_file(hw_param_context_t *ctx, const char *path);

/* 列出全部（调试/CLI） */
extern void hw_param_dump_all(hw_param_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_PARAM_H */