/*
 * storage_core.c — STORAGE 协议插件主入口与生命周期
 *
 * 实现 hw_plugin_entry()：返回 hw_plugin_t*，其 ops 提供
 * init/start/stop/destroy/configure/get_interface。get_interface("STORAGE")
 * 返回 hw_storage_ops_t*（真实实现，见 storage_impl.c：用户态目录卷管理，
 * statvfs/du 统计与目录级快照均为真实文件系统操作，绝无占位/假数据）。
 *
 * 协议字符串使用字面量 "STORAGE"（根 include/hwrun.h 未定义 HWPROTO_STORAGE，
 * 不引用未定义宏）。
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include "hwrun.h"
#include "hwrun_plugin.h"
#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 由实现文件提供（storage_impl.c） */
extern hw_storage_ops_t hw_storage_ops;
extern int storage_impl_init(void);
extern void storage_impl_shutdown(void);

/* ---- 生命周期 ---- */
static int g_plugin_state = HWPLUGIN_LOADED;

static int storage_plugin_init(hw_plugin_t *self) {
    (void)self;
    int rc = storage_impl_init(); /* 上下文复位 + 载入 $HWRUN_STATE/storage.state */
    if (rc != 0) {
        HWAPI_LOGE("storage", "init: 注册表加载失败 rc=%d", rc);
        return rc;
    }
    g_plugin_state = HWPLUGIN_LOADED;
    HWAPI_LOGI("storage", "init: 用户态卷管理就绪（storage.state 已重载）");
    return HWRUN_OK;
}

static int storage_plugin_start(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_STARTED;
    return HWRUN_OK;
}

static int storage_plugin_stop(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_STOPPED;
    return HWRUN_OK;
}

static int storage_plugin_destroy(hw_plugin_t *self) {
    (void)self;
    storage_impl_shutdown(); /* 释放卷/快照注册表与锁 */
    g_plugin_state = HWPLUGIN_UNINSTALLED;
    return HWRUN_OK;
}

/* 参数变更回调：当前无运行时配置项，返回 0 表示接受 */
static int storage_plugin_configure(hw_plugin_t *self, const char *key, const char *value) {
    (void)self;
    (void)key;
    (void)value;
    return HWRUN_OK;
}

/* get_interface：对外提供 STORAGE 协议接口指针 */
static void *storage_plugin_get_interface(const char *protocol) {
    if (!protocol) return NULL;
    if (strcmp(protocol, "STORAGE") == 0) return &hw_storage_ops;
    return NULL;
}

static hw_plugin_ops_t g_ops = {
    .init = storage_plugin_init,
    .start = storage_plugin_start,
    .stop = storage_plugin_stop,
    .destroy = storage_plugin_destroy,
    .configure = storage_plugin_configure,
    .get_interface = storage_plugin_get_interface,
};

/* 协议清单：以 NULL 哨兵结尾的只读数组（.so 静态数据，与 plugin.yml 一致） */
static const char *const g_provides[] = {"STORAGE", NULL};
static const char *const g_requires[] = {"LOG", "PARAM", "METAPROTO", NULL};

HWRUN_PLUGIN_BIND()
HWRUN_PLUGIN_DEFINE("storage", "Storage Backend Protocol", "1.0.0", HWPLUGIN_TYPE_STORAGE,
                    "HWRun OS STORAGE protocol: 用户态卷管理（目录卷注册表/生命周期/统计/快照）",
                    &g_ops, g_provides, g_requires)

#ifdef __cplusplus
}
#endif
