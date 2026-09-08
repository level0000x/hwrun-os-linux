/*
 * loader.c — HWRun OS 可执行文件加载协议（LOADER）插件主入口
 *
 * 实现 hw_plugin_t 生命周期（init/start/stop/destroy），
 * 并通过 get_interface("LOADER") 返回 hw_loader_ops_t 协议接口。
 *
 * 本插件作为独立 .so 提供给总线 dlopen 加载，导出 hw_plugin_entry()。
 */

#include "hwrun.h"
#include "hwrun_plugin.h"

#include "../include/loader.h"

/* ---- 协议接口转发（供 get_interface 返回） ---- */
static hw_loader_ops_t g_loader_ops = {
    .detect      = loader_detect_impl,
    .elf_parse   = loader_elf_impl,
    .load        = loader_load_impl,
    .run         = loader_run_impl,
    .unload      = loader_unload_impl,
    .get_formats = loader_formats_impl,
};

/* 插件私有上下文 */
typedef struct loader_pdata {
    int started;
} loader_pdata_t;

/* ============================================================
 * 生命周期回调
 * ============================================================ */
static int loader_init(hw_plugin_t *self) {
    loader_pdata_t *pd;
    if (!self) return HWRUN_EINVAL;

    pd = calloc(1, sizeof(loader_pdata_t));
    if (!pd) return HWRUN_ENOMEM;
    pd->started = 0;
    self->private_data = pd;
    HWAPI_LOGI("loader", "init: formats=%d",
               HWAPI_PARAM_GET_INT("loader.formats", 0));
    return HWRUN_OK;
}

static int loader_start(hw_plugin_t *self) {
    loader_pdata_t *pd = self ? (loader_pdata_t *)self->private_data : NULL;
    if (!pd) return HWRUN_ENOTREADY;
    if (pd->started) return HWRUN_OK;

    /* 启动自检：预热格式列表，失败不影响启动 */
    loader_format_info_t fmts[8];
    (void)loader_formats_impl(fmts, 8);

    pd->started = 1;
    self->state = HWPLUGIN_STARTED;
    return HWRUN_OK;
}

static int loader_stop(hw_plugin_t *self) {
    loader_pdata_t *pd = self ? (loader_pdata_t *)self->private_data : NULL;
    if (!pd) return HWRUN_OK;
    pd->started = 0;
    return HWRUN_OK;
}

static int loader_destroy(hw_plugin_t *self) {
    loader_pdata_t *pd = self ? (loader_pdata_t *)self->private_data : NULL;
    if (pd) free(pd);
    if (self) self->private_data = NULL;
    return HWRUN_OK;
}

/* 参数变更回调：本插件暂无可配置参数，一律接受 */
static int loader_configure(hw_plugin_t *self, const char *key,
                            const char *value) {
    (void)self; (void)key; (void)value;
    return HWRUN_OK;
}

/* get_interface：返回对外提供的协议实现 */
static void *loader_get_interface(const char *protocol) {
    if (protocol && strcmp(protocol, HWPROTO_LOADER) == 0) {
        return &g_loader_ops;
    }
    return NULL;
}

/* 生命周期 ops 表：由 SDK 宏装配进描述符 */
static hw_plugin_ops_t g_ops = {
    .init          = loader_init,
    .start         = loader_start,
    .stop          = loader_stop,
    .destroy       = loader_destroy,
    .configure     = loader_configure,
    .get_interface = loader_get_interface,
};

/* 协议清单：以 NULL 哨兵结尾的只读数组（.so 静态数据） */
static const char *const g_provides[] = { "LOADER", NULL };
static const char *const g_requires[] = {
    "HAP", "PMP", "FSP", "LOG", "PARAM", "METAPROTO", NULL
};

HWRUN_PLUGIN_BIND()
HWRUN_PLUGIN_DEFINE(
    "loader", "Executable Loader Protocol", "1.0.0", HWPLUGIN_TYPE_LOADER,
    "HWRun OS 可执行文件加载协议：检测 / 加载 / 运行各类可执行格式",
    &g_ops, g_provides, g_requires)