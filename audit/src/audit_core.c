/*
 * audit_core.c — AUDIT 协议插件主入口与生命周期
 *
 * 实现 hw_plugin_entry()：返回 hw_plugin_t*，其 ops 提供
 * init/start/stop/destroy/configure/get_interface。get_interface("AUDIT")
 * 返回 hw_audit_ops_t*（真实用户态事件审计实现，见 audit_impl.c）。
 *
 * 生命周期语义：
 *   - init()    只复位插件状态（日志目录在首次使用时按需创建/回填序号）；
 *   - start()   读取总线 PARAM 注入的 audit.log.* 参数（参数风格路径覆盖），
 *               未注入时使用头文件缺省值（默认审计状态目录 /var/lib/hwrun/audit）；
 *   - configure() 参数变更回调，直接转交实现层 hw_audit_configure()。
 *
 * 与设计稿《HWRun OS AUDIT.txt》边界：设计稿为内核模块（LSM 钩子 + 规则引擎），
 * 本实现按 docs/design.md 收敛为用户态 .so 插件；内核级自动审计点不落地，
 * 上层经本协议显式记录事件。设计稿依赖的 FSP/SP/PERMISSION 为规则/策略维度
 * 所需，P0 用户态事件审计不实际调用，故 requires 仅取最小运行集 LOG/PARAM/
 * METAPROTO（与 COMPRESS/GIT 等现有插件一致）。
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "hwrun.h"
#include "hwrun_plugin.h"
#include "audit.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 由实现文件提供（audit_impl.c） */
extern hw_audit_ops_t hw_audit_ops;

/* 由实现文件提供的参数入口（audit_impl.c），configure/start 共用 */
extern int hw_audit_configure(const char *key, const char *value);

/* ---- 生命周期 ---- */
static int g_plugin_state = HWPLUGIN_LOADED;

static int audit_plugin_init(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_LOADED;
    HWAPI_LOGI("audit", "init: user-space event audit (AUDIT/1.0)");
    return HWRUN_OK;
}

static int audit_plugin_start(hw_plugin_t *self) {
    (void)self;
    /* 总线 PARAM 注入优先于头文件缺省值（参数风格路径覆盖） */
    const char *v = HWAPI_PARAM_GET(HW_AUDIT_PARAM_PATH);
    if (v && v[0]) hw_audit_configure(HW_AUDIT_PARAM_PATH, v);
    v = HWAPI_PARAM_GET(HW_AUDIT_PARAM_MAX_BYTES);
    if (v && v[0]) hw_audit_configure(HW_AUDIT_PARAM_MAX_BYTES, v);
    v = HWAPI_PARAM_GET(HW_AUDIT_PARAM_MAX_FILES);
    if (v && v[0]) hw_audit_configure(HW_AUDIT_PARAM_MAX_FILES, v);
    g_plugin_state = HWPLUGIN_STARTED;
    HWAPI_LOGI("audit", "started: log dir from param '%s' or default", HW_AUDIT_PARAM_PATH);
    return HWRUN_OK;
}

static int audit_plugin_stop(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_STOPPED;
    return HWRUN_OK;
}

static int audit_plugin_destroy(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_UNINSTALLED;
    return HWRUN_OK;
}

/* 参数变更回调：audit.log.path / max_bytes / max_files 转实现层，未知键接受 */
static int audit_plugin_configure(hw_plugin_t *self, const char *key, const char *value) {
    (void)self;
    return hw_audit_configure(key, value);
}

/* get_interface：对外提供 AUDIT 协议接口指针（字面量 "AUDIT"，无预定义宏） */
static void *audit_plugin_get_interface(const char *protocol) {
    if (!protocol) return NULL;
    if (strcmp(protocol, "AUDIT") == 0) return &hw_audit_ops;
    return NULL;
}

static hw_plugin_ops_t g_ops = {
    .init = audit_plugin_init,
    .start = audit_plugin_start,
    .stop = audit_plugin_stop,
    .destroy = audit_plugin_destroy,
    .configure = audit_plugin_configure,
    .get_interface = audit_plugin_get_interface,
};

/* 协议清单：以 NULL 哨兵结尾的只读数组（.so 静态数据，与 plugin.yml 一致） */
static const char *const g_provides[] = {"AUDIT", NULL};
/* 最小运行依赖。未含设计稿的 FSP/SP/PERMISSION/COMPRESS：v1 事件审计自包含，
 * 规则/策略/日志压缩均未接线，requires 越少越利于独立 dlopen 与总线按需加载。 */
static const char *const g_requires[] = {"LOG", "PARAM", "METAPROTO", NULL};

HWRUN_PLUGIN_BIND()
HWRUN_PLUGIN_DEFINE("audit", "Audit Protocol", "1.0.0", HWPLUGIN_TYPE_SECURITY,
                    "HWRun OS AUDIT protocol: user-space event audit (append/query/export/rotate)",
                    &g_ops, g_provides, g_requires)

#ifdef __cplusplus
}
#endif
