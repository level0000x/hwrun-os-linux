/*
 * hap.c — HWRun OS 硬件抽象协议（HAP）插件主入口
 *
 * 实现 hw_plugin_t 生命周期（init/start/stop/destroy），
 * 并通过 get_interface("HAP") 返回 hw_hap_ops_t 协议接口。
 *
 * 本插件作为独立 .so 提供给总线 dlopen 加载，导出 hw_plugin_entry()。
 */

#include "hwrun.h"

#include <dlfcn.h>

#include "../include/hap.h"

/* ---- 协议接口转发（薄封装，供 get_interface 返回） ---- */
static hw_hap_ops_t g_hap_ops = {
    .get_cpu_info     = hap_cpu_probe,
    .get_memory_info  = hap_memory_probe,
    .get_disk_info    = hap_disk_probe,
    .get_net_info     = hap_net_probe,
    .get_system_info  = hap_system_probe,
};

/* 插件私有上下文（保存句柄/状态，供 lifecycle 使用） */
typedef struct hap_pdata {
    int started;
} hap_pdata_t;

/* ============================================================
 * 生命周期回调
 * ============================================================ */
static int hap_init(hw_plugin_t *self) {
    hap_pdata_t *pd;
    if (!self) return HWRUN_EINVAL;

    pd = calloc(1, sizeof(hap_pdata_t));
    if (!pd) return HWRUN_ENOMEM;
    pd->started = 0;
    self->private_data = pd;
    return HWRUN_OK;
}

static int hap_start(hw_plugin_t *self) {
    hap_cpu_info_t cpu;
    hap_pdata_t *pd = self ? (hap_pdata_t *)self->private_data : NULL;
    if (!pd) return HWRUN_ENOTREADY;
    if (pd->started) return HWRUN_OK;

    /* 探测一次 CPU，作为启动自检并预热缓存（失败不影响启动） */
    memset(&cpu, 0, sizeof(cpu));
    hap_cpu_probe(&cpu);

    pd->started = 1;
    self->state = HWPLUGIN_STARTED;
    return HWRUN_OK;
}

static int hap_stop(hw_plugin_t *self) {
    hap_pdata_t *pd = self ? (hap_pdata_t *)self->private_data : NULL;
    if (!pd) return HWRUN_OK;
    pd->started = 0;
    return HWRUN_OK;
}

static int hap_destroy(hw_plugin_t *self) {
    hap_pdata_t *pd = self ? (hap_pdata_t *)self->private_data : NULL;
    if (pd) free(pd);
    if (self) self->private_data = NULL;
    return HWRUN_OK;
}

/* 参数变更回调：本插件暂无可配置参数，一律接受 */
static int hap_configure(hw_plugin_t *self, const char *key, const char *value) {
    (void)self; (void)key; (void)value;
    return HWRUN_OK;
}

/* get_interface：返回对外提供的协议实现 */
static void *hap_get_interface(const char *protocol) {
    if (protocol && strcmp(protocol, HWPROTO_HAP) == 0) {
        return &g_hap_ops;
    }
    return NULL;
}

static hw_plugin_t g_hap_plugin;

/* ============================================================
 * 插件入口：dlopen 加载时调用，返回 hw_plugin_t*
 * ============================================================ */
__attribute__((visibility("default")))
hw_plugin_t *hw_plugin_entry(void) {
    memset(&g_hap_plugin, 0, sizeof(g_hap_plugin));

    /* 基础元数据（与 plugin.yml 保持一致） */
    strncpy(g_hap_plugin.id, "hap", sizeof(g_hap_plugin.id) - 1);
    strncpy(g_hap_plugin.name, "Hardware Abstraction Protocol",
            sizeof(g_hap_plugin.name) - 1);
    strncpy(g_hap_plugin.version, "1.0.0", sizeof(g_hap_plugin.version) - 1);
    g_hap_plugin.type = HWPLUGIN_TYPE_KERNEL;
    g_hap_plugin.state = HWPLUGIN_INSTALLED;
    strncpy(g_hap_plugin.description,
            "HWRun OS 硬件抽象协议，提供对 CPU、内存、磁盘、网络、系统信息的统一访问",
            sizeof(g_hap_plugin.description) - 1);

    /* 提供的协议 */
    static char *hap_provides[] = {
        "HAP",
    };
    g_hap_plugin.provides = hap_provides;
    g_hap_plugin.provides_count = 1;

    /* 依赖的协议（仅声明，真正校验由总线 && METAPROTO 完成） */
    static char *hap_requires[] = {
        "LOG", "PARAM", "METAPROTO",
    };
    g_hap_plugin.requires = hap_requires;
    g_hap_plugin.requires_count = 3;

    /* 生命周期 */
    g_hap_plugin.ops.init         = hap_init;
    g_hap_plugin.ops.start        = hap_start;
    g_hap_plugin.ops.stop         = hap_stop;
    g_hap_plugin.ops.destroy      = hap_destroy;
    g_hap_plugin.ops.configure    = hap_configure;
    g_hap_plugin.ops.get_interface = hap_get_interface;

    return &g_hap_plugin;
}