/*
 * pmp.c — HWRun OS PMP（进程管理协议）插件入口
 *
 * 依赖链第 4 环（HAP -> PMP）。以 .so 导出 hw_plugin_entry()，
 * get_interface("PMP") 返回 hw_pmp_ops_t*。
 *
 * 生命周期：init() -> start() -> stop() -> destroy()。
 */

#include "pmp.h"
#include "hwrun.h"
#include "hwrun_plugin.h"

#include <stdio.h>
#include <string.h>

/* 各实现（同插件内多个 .c） */
int        pmp_get_process_list(pmp_process_info_t **out, uint32_t *cnt);
void       pmp_free_process_list(pmp_process_info_t *list, uint32_t cnt);
int        pmp_get_process_info(int32_t pid, pmp_process_info_t *out);
int        pmp_get_current_pid(void);
int        pmp_fork(void);
int        pmp_exec(const char *path, char * const argv[]);
int        pmp_execve_full(const char *path, char * const argv[], char * const envp[]);
int        pmp_spawn(const char *path, char * const argv[], int *out_pid);
int        pmp_wait(int32_t pid, int *status, int options);
int        pmp_signal(int32_t pid, int sig);
int        pmp_kill(int32_t pid, int sig);
int        pmp_start(int32_t pid);
int        pmp_stop(int32_t pid);
int        pmp_pause_process(int32_t pid);
int        pmp_resume(int32_t pid);
int        pmp_sched_set(int32_t pid, pmp_sched_policy_t policy, int32_t prio, int32_t nice);
int        pmp_sched_get(int32_t pid, pmp_sched_policy_t *policy, int32_t *prio, int *nice);
int        pmp_set_affinity(int32_t pid, uint32_t mask);
int        pmp_get_affinity(int32_t pid, uint32_t *mask);
uint64_t   pmp_task_submit(const char *name, const char *path, char * const argv[], int *out_err);
int        pmp_task_cancel(uint64_t task_id);
int        pmp_task_status(uint64_t task_id, pmp_task_info_t *out);
void       pmp_task_deinit(void);

/* 本插件私有的上下文 */
typedef struct pmp_context {
    int initialized;
    unsigned *reserved;
} pmp_context_t;

static pmp_context_t g_ctx;

/* 对外暴露的 PMP 协议接口表 */
static hw_pmp_ops_t g_pmp_ops = {
    .get_process_list = pmp_get_process_list,
    .free_process_list = pmp_free_process_list,
    .get_process_info  = pmp_get_process_info,
    .get_current_pid   = pmp_get_current_pid,
    .fork              = pmp_fork,
    .exec              = pmp_exec,
    .execve_full       = pmp_execve_full,
    .spawn             = pmp_spawn,
    .wait              = pmp_wait,
    .signal            = pmp_signal,
    .kill              = pmp_kill,
    .start             = pmp_start,
    .stop              = pmp_stop,
    .pause_process     = pmp_pause_process,
    .resume            = pmp_resume,
    .sched_set         = pmp_sched_set,
    .sched_get         = pmp_sched_get,
    .set_affinity      = pmp_set_affinity,
    .get_affinity      = pmp_get_affinity,
    .task_submit       = pmp_task_submit,
    .task_cancel       = pmp_task_cancel,
    .task_status       = pmp_task_status,
};

/* ============================================================
 * 生命周期回调
 * ============================================================ */
static int pmp_plugin_init(hw_plugin_t *self) {
    (void)self;
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.initialized = 1;
    HWAPI_LOGI("pmp", "init: task_pool=%d",
               HWAPI_PARAM_GET_INT("pmp.task_pool", 32));
    return HWRUN_OK;                    /* 0 成功 */
}

static int pmp_plugin_start(hw_plugin_t *self) {
    (void)self;
    if (!g_ctx.initialized) return HWRUN_ENOTREADY;
    return HWRUN_OK;
}

static int pmp_plugin_stop(hw_plugin_t *self) {
    (void)self;
    return HWRUN_OK;
}

static int pmp_plugin_destroy(hw_plugin_t *self) {
    (void)self;
    pmp_task_deinit();                  /* 清理内部任务表 */
    g_ctx.initialized = 0;
    return HWRUN_OK;
}

static int pmp_plugin_configure(hw_plugin_t *self, const char *key,
                                const char *value) {
    (void)self; (void)key; (void)value;
    return HWRUN_OK;
}

/* get_interface：返回本插件提供的协议实现 */
static void *pmp_get_interface(const char *protocol) {
    if (!protocol) return NULL;
    if (strcmp(protocol, HWPROTO_PMP) == 0)
        return &g_pmp_ops;
    return NULL;
}

/* 生命周期回调表（SDK 宏将其拷贝进描述符 g_hwplugin.ops） */
static hw_plugin_ops_t pmp_ops = {
    .init          = pmp_plugin_init,
    .start         = pmp_plugin_start,
    .stop          = pmp_plugin_stop,
    .destroy       = pmp_plugin_destroy,
    .configure     = pmp_plugin_configure,
    .get_interface = pmp_get_interface,
};

/* ============================================================
 * 运行时注入绑定点 + 插件描述符（统一由 hwrun_plugin.h 宏生成）
 * ============================================================ */
HWRUN_PLUGIN_BIND()

/* 协议与依赖声明（plugin.yml 是对外的权威描述；.so 内自持一份） */
static const char *const g_provides[] = { HWPROTO_PMP, NULL };
static const char *const g_requires[] = {
    HWPROTO_METAPROTO, HWPROTO_PARAM, HWPROTO_LOG, HWPROTO_HAP, NULL,
};

HWRUN_PLUGIN_DEFINE("pmp", "Process Management Protocol", "1.0.0",
                    HWPLUGIN_TYPE_KERNEL,
                    "HWRun OS 进程管理协议：进程创建/控制/调度/信号/任务管理",
                    &pmp_ops, g_provides, g_requires)