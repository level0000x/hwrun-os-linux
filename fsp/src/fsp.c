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
#include "hwrun_plugin.h"
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

static int fsp_init(hw_plugin_t *self) {
    (void)self;
    /* 组装 FSP 协议接口：文件/目录/权限/路径/挂载 */
    hw_fsp_ops_file_init(&g_fsp_ops);
    hw_fsp_ops_dir_init(&g_fsp_ops);
    hw_fsp_ops_perm_init(&g_fsp_ops);
    hw_fsp_ops_path_init(&g_fsp_ops);
    hw_fsp_ops_mount_init(&g_fsp_ops);

    HWAPI_LOGI("fsp", "init: root=%s",
               HWAPI_PARAM_GET("fsp.root") ? HWAPI_PARAM_GET("fsp.root") : "/");

    self->state = HWPLUGIN_LOADED;
    return HWRUN_OK; /* 成功 */
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

/* 动态配置：fsp.root 变更时校验并记录（总线 param->configure 路由调用）。
 * fsp.root 为空视为复位为 "/"；此实现示范 configure 链路端到端可用。 */
static int fsp_configure(hw_plugin_t *self, const char *key, const char *value) {
    (void)self;
    if (strcmp(key, "fsp.root") == 0) {
        const char *root = (value && value[0]) ? value : "/";
        HWAPI_LOGI("fsp", "configure: fsp.root=%s", root);
        return HWRUN_OK;
    }
    /* 非本插件管理的 key：静默接受（路由已按插件 id 前缀过滤，正常不会到这） */
    return HWRUN_OK;
}

static int fsp_destroy(hw_plugin_t *self) {
    (void)self;
    self->state = HWPLUGIN_UNINSTALLED;
    return HWRUN_OK;
}

/* 取得对外提供的协议接口实现指针 */
static void *fsp_get_interface(const char *protocol) {
    if (protocol && strcmp(protocol, HWPROTO_FSP) == 0) return &g_fsp_ops;
    return NULL;
}

/* 生命周期回调表（SDK 宏将其拷贝进描述符 g_hwplugin.ops） */
static hw_plugin_ops_t fsp_ops = {
    .init = fsp_init,
    .start = fsp_start,
    .stop = fsp_stop,
    .destroy = fsp_destroy,
    .configure = fsp_configure,
    .get_interface = fsp_get_interface,
};

/* ============================================================
 * 运行时注入绑定点 + 插件描述符（统一由 hwrun_plugin.h 宏生成）
 * ============================================================ */
HWRUN_PLUGIN_BIND()

/* 协议与依赖声明（plugin.yml 是对外的权威描述；.so 内自持一份） */
static const char *const g_provides[] = {HWPROTO_FSP, NULL};
static const char *const g_requires[] = {
    HWPROTO_HAP, HWPROTO_PMP, HWPROTO_LOG, HWPROTO_PARAM, HWPROTO_METAPROTO, NULL,
};

HWRUN_PLUGIN_DEFINE("fsp", "FileSystem Protocol", "1.0.0", HWPLUGIN_TYPE_FS,
                    "HWRun OS 文件系统协议：文件/目录/权限/路径/挂载操作", &fsp_ops, g_provides,
                    g_requires)