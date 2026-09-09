/*
 * permission_core.c — PERMISSION 协议插件主入口与生命周期
 *
 * 实现 hw_plugin_entry()：返回 hw_plugin_t*，其 ops 提供
 * init/start/stop/destroy/configure/get_interface。get_interface("PERMISSION")
 * 返回 hw_permission_ops_t*（真实用户态 RBAC 实现，见 permission_impl.c）。
 * start 时按 env（PERMISSION_STATE / HWRUN_STATE）解析状态文件路径并载入
 * 既有策略——重启可恢复；destroy 释放内存策略。
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include "hwrun.h"
#include "hwrun_plugin.h"
#include "permission.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 由实现文件提供（permission_impl.c） */
extern hw_permission_ops_t hw_permission_ops;
extern int hw_permission_state_open(void);
extern void hw_permission_state_close(void);

/* ---- 生命周期 ---- */
static int g_plugin_state = HWPLUGIN_LOADED;

static int permission_plugin_init(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_LOADED;
    HWAPI_LOGI("permission", "init: user-space RBAC");
    return HWRUN_OK;
}

static int permission_plugin_start(hw_plugin_t *self) {
    (void)self;
    int rc = hw_permission_state_open();
    if (rc != HWRUN_OK) HWAPI_LOGW("permission", "start: state load rc=%d", rc);
    g_plugin_state = HWPLUGIN_STARTED;
    return HWRUN_OK;
}

static int permission_plugin_stop(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_STOPPED;
    return HWRUN_OK;
}

static int permission_plugin_destroy(hw_plugin_t *self) {
    (void)self;
    hw_permission_state_close();
    g_plugin_state = HWPLUGIN_UNINSTALLED;
    return HWRUN_OK;
}

/* 参数变更回调：当前无运行时配置项，返回 0 表示接受 */
static int permission_plugin_configure(hw_plugin_t *self, const char *key, const char *value) {
    (void)self;
    (void)key;
    (void)value;
    return HWRUN_OK;
}

/* get_interface：对外提供 PERMISSION 协议接口指针 */
static void *permission_plugin_get_interface(const char *protocol) {
    if (!protocol) return NULL;
    if (strcmp(protocol, "PERMISSION") == 0) return &hw_permission_ops;
    return NULL;
}

static hw_plugin_ops_t g_ops = {
    .init = permission_plugin_init,
    .start = permission_plugin_start,
    .stop = permission_plugin_stop,
    .destroy = permission_plugin_destroy,
    .configure = permission_plugin_configure,
    .get_interface = permission_plugin_get_interface,
};

/* 协议清单：以 NULL 哨兵结尾的只读数组（.so 静态数据，与 plugin.yml 一致） */
static const char *const g_provides[] = {"PERMISSION", NULL};
static const char *const g_requires[] = {"LOG", "PARAM", "METAPROTO", NULL};

HWRUN_PLUGIN_BIND()
HWRUN_PLUGIN_DEFINE(
    "permission", "Permission Management Protocol", "1.0.0", HWPLUGIN_TYPE_SECURITY,
    "HWRun OS PERMISSION protocol: user-space RBAC (subject-role-permission, default deny)", &g_ops,
    g_provides, g_requires)

#ifdef __cplusplus
}
#endif
