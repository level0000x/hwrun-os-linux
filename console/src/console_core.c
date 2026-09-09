/*
 * console_core.c — CONSOLE 协议插件主入口与生命周期
 *
 * 实现 hw_plugin_entry()：返回 hw_plugin_t*，其 ops 提供
 * init/start/stop/destroy/configure/get_interface。get_interface("CONSOLE")
 * 返回 hw_console_ops_t*（真实实现，基于内存行缓冲的读终端抽象，零内核依赖）。
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include "hwrun.h"
#include "hwrun_plugin.h"
#include "console.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 由实现文件提供（console_impl.c） */
extern hw_console_ops_t hw_console_ops;

/* ---- 生命周期 ---- */
static int g_plugin_state = HWPLUGIN_LOADED;

static int console_plugin_init(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_LOADED;
    HWAPI_LOGI("console", "init: sessions=memory-backed, kernel-independent");
    return HWRUN_OK;
}

static int console_plugin_start(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_STARTED;
    return HWRUN_OK;
}

static int console_plugin_stop(hw_plugin_t *self) {
    (void)self;
    hw_console_cleanup();
    g_plugin_state = HWPLUGIN_STOPPED;
    return HWRUN_OK;
}

static int console_plugin_destroy(hw_plugin_t *self) {
    (void)self;
    hw_console_cleanup();
    g_plugin_state = HWPLUGIN_UNINSTALLED;
    return HWRUN_OK;
}

/* 参数变更回调：当前无运行时配置项，返回 0 表示接受 */
static int console_plugin_configure(hw_plugin_t *self, const char *key, const char *value) {
    (void)self;
    (void)key;
    (void)value;
    return HWRUN_OK;
}

/* get_interface：对外提供 CONSOLE 协议接口指针 */
static void *console_plugin_get_interface(const char *protocol) {
    if (!protocol) return NULL;
    if (strcmp(protocol, HWPROTO_CONSOLE) == 0) return &hw_console_ops;
    return NULL;
}

static hw_plugin_ops_t g_ops = {
    .init = console_plugin_init,
    .start = console_plugin_start,
    .stop = console_plugin_stop,
    .destroy = console_plugin_destroy,
    .configure = console_plugin_configure,
    .get_interface = console_plugin_get_interface,
};

/* 协议清单：以 NULL 哨兵结尾的只读数组（.so 静态数据，与 plugin.yml 一致） */
static const char *const g_provides[] = {"CONSOLE", NULL};
static const char *const g_requires[] = {"LOG", "PARAM", "METAPROTO", NULL};

HWRUN_PLUGIN_BIND()
HWRUN_PLUGIN_DEFINE(
    "console", "System Console", "1.0.0", HWPLUGIN_TYPE_TOOLS,
    "HWRun OS CONSOLE protocol: session mgmt, banner, line/raw I/O, status, output attach", &g_ops,
    g_provides, g_requires)

#ifdef __cplusplus
}
#endif