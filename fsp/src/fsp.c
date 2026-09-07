/*
 * fsp.c — FSP（FileSystem Protocol）插件主文件
 *
 * 依赖链第 5 环（HAP → PMP → FSP）。导出 hw_plugin_entry()，
 * 返回 hw_plugin_t*，向总线注册协议 FSP。
 *
 * 本插件以下层到底层全部基于 POSIX 文件系统接口（open/read/write/opendir 等），
 * 在 MSYS2 / Linux 原生环境真实可用，无需内核态。此处为传统插件风格：
 * 通过 hw_plugin_ops_t::get_interface("FSP") 暴露 hw_fsp_ops_t*。
 */

#include "hwrun.h"
#include "fsp.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

/* 各模块实现的前置声明（hw_fsp_ops_t 的填充函数） */
extern void hw_fsp_ops_file_init(hw_fsp_ops_t *ops);
extern void hw_fsp_ops_dir_init(hw_fsp_ops_t *ops);
extern void hw_fsp_ops_perm_init(hw_fsp_ops_t *ops);
extern void hw_fsp_ops_path_init(hw_fsp_ops_t *ops);
extern void hw_fsp_ops_mount_init(hw_fsp_ops_t *ops);

/* 全局 FSP 接口实例 */
static hw_fsp_ops_t g_fsp_ops;

static hw_plugin_t g_plugin;   /* 插件描述符 */

static int fsp_init(hw_plugin_t *self) {
    (void)self;
    /* 组装 FSP 协议接口：文件/目录/权限/路径/挂载 */
    hw_fsp_ops_file_init(&g_fsp_ops);
    hw_fsp_ops_dir_init(&g_fsp_ops);
    hw_fsp_ops_perm_init(&g_fsp_ops);
    hw_fsp_ops_path_init(&g_fsp_ops);
    hw_fsp_ops_mount_init(&g_fsp_ops);

    self->state = HWPLUGIN_LOADED;
    return HWRUN_OK;   /* 成功 */
}

static int fsp_start(hw_plugin_t *self) {
    (void)self;
    self->state = HWPLUGIN_STARTED;
    return HWRUN_OK;
}

static int fsp_stop(hw_plugin_t *self) {
    (void)self;
    self->state = HWPLUGIN_STOPPED;
    return HWRUN_OK;
}

static int fsp_destroy(hw_plugin_t *self) {
    (void)self;
    self->state = HWPLUGIN_UNINSTALLED;
    return HWRUN_OK;
}

/* 取得对外提供的协议接口实现指针 */
static void *fsp_get_interface(const char *protocol) {
    if (protocol && strcmp(protocol, HWPROTO_FSP) == 0)
        return &g_fsp_ops;
    return NULL;
}

static hw_plugin_ops_t fsp_ops = {
    .init         = fsp_init,
    .start        = fsp_start,
    .stop         = fsp_stop,
    .destroy      = fsp_destroy,
    .configure    = NULL,
    .get_interface= fsp_get_interface,
};

/* .so 统一入口：返回 hw_plugin_t* */
hw_plugin_t *hw_plugin_entry(void) {
    memset(&g_plugin, 0, sizeof(g_plugin));

    snprintf(g_plugin.id,          sizeof(g_plugin.id),          "%s", "fsp");
    snprintf(g_plugin.name,        sizeof(g_plugin.name),        "%s", "FileSystem Protocol");
    snprintf(g_plugin.version,     sizeof(g_plugin.version),     "%s", "1.0.0");
    g_plugin.type   = HWPLUGIN_TYPE_FS;
    g_plugin.state  = HWPLUGIN_INSTALLED;
    snprintf(g_plugin.description, sizeof(g_plugin.description),
             "%s", "HWRun OS 文件系统协议：文件/目录/权限/路径/挂载操作");

    /* 提供的协议 */
    static char *provides[] = { (char*)"FSP" };
    g_plugin.provides       = provides;
    g_plugin.provides_count = 1;

    /* 依赖协议（依赖链顺序） */
    static char *requires[] = { (char*)"HAP", (char*)"PMP", (char*)"LOG",
                                (char*)"PARAM", (char*)"METAPROTO" };
    g_plugin.requires       = requires;
    g_plugin.requires_count = 5;

    g_plugin.ops = fsp_ops;
    return &g_plugin;
}