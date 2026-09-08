/*
 * bus.h — 组件总线接口
 *
 * 系统的"插件管理器"：发现、加载、生命周期、协议注册、依赖校验、热插拔。
 * 依赖链第二环。作为 PID1/CLI 运行。
 */

#ifndef HWRUN_BUS_H
#define HWRUN_BUS_H

#include "hwrun.h"
#include "hwlock.h"
#include "metaproto.h"
#include "param.h"
#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HWRUN_SCAN_PATH_MAX 8
#define HWRUN_SEQ_BUFF 64

/* 组件总线上下文 */
typedef struct hw_bus {
    hw_plugin_t *plugins;         /* 插件链表           */
    hw_locker_t plugins_lock;     /* HWLOCK_RW：保护 plugins 链表结构 */
    hw_metaproto_registry_t meta; /* METAPROTO 注册表   */
    hw_param_context_t params;    /* 参数系统           */
    hw_log_ctx_t log;             /* 日志系统           */

    char scan_paths[HWRUN_SCAN_PATH_MAX][256];
    int scan_paths_count;
    int auto_install_deps;
    int dry_run;
    char git_remote[256];
    char state_dir[256]; /* /var/lib/hwrun     */

    int initialized;
    int running;
    int param_git_committing; /* mark_revision->GIT commit 重入守卫 */
} hw_bus_t;

/* 插件发现信息（从 plugin.yml 解析） */
typedef struct hw_plugin_discovery {
    char id[64], name[128], version[32], type[32];
    char description[256];
    char repo[256], branch[64], tag[64];
    char *provides[32];
    int provides_count;
    char *requires[32];
    int requires_count;
    char *conflicts[16];
    int conflicts_count;
    char *files[64];
    int files_count;
    size_t size;
    char license[32], author[128], url[256];
    char pre_install[128], post_install[128];
    char pre_uninstall[128], post_uninstall[128];
    hw_param_t *params;
    struct hw_plugin_discovery *next;
} hw_plugin_discovery_t;

/* ============================================================
 * BUS 主接口
 * ============================================================ */
extern int hw_bus_init(hw_bus_t *bus, const char *state_dir, const char *scan_path, int level);
extern void hw_bus_shutdown(hw_bus_t *bus);
extern int hw_bus_scan(hw_bus_t *bus);                 /* 扫描插件目录 */
extern int hw_bus_load(hw_bus_t *bus, const char *id); /* 加载并启动一个插件 */
extern int hw_bus_boot_chain(hw_bus_t *bus);           /* 按依赖链依序启动 */
extern int hw_bus_stop(hw_bus_t *bus, const char *id);
extern int hw_bus_unload(hw_bus_t *bus, const char *id);
extern int hw_bus_resolve(hw_bus_t *bus, const char *protocol, hw_protocol_route_t **out);

/* 状态查询 */
extern hw_plugin_t *hw_bus_find(hw_bus_t *bus, const char *id);
extern int hw_bus_plugin_count(hw_bus_t *bus);

/* 参数变更 -> 插件 configure 动态配置路由：
 * 对形如 "<plugin_id>.<key>" 的参数变更，找到已启动插件并调其
 * ops.configure(self, key, value)。boot_chain 完成后调用一次即可。 */
extern int hw_bus_config_route(hw_bus_t *bus);

/* ============================================================
 * plugin.yml 解析（bus/yml.c）
 * ============================================================ */
extern int hw_yml_parse_plugin(const char *yml_path, hw_plugin_discovery_t *d);
extern void hw_plugin_discovery_clear(hw_plugin_discovery_t *d);
extern void hw_plugin_discovery_free(hw_plugin_discovery_t *d);
extern int hw_type_from_str(const char *s);
extern const char *hw_type_to_str(int type);

/* ============================================================
 * 插件加载（bus/loader.c）
 * ============================================================ */
extern int hw_plugin_load_so(hw_bus_t *bus, hw_plugin_t *p);
extern int hw_plugin_start(hw_bus_t *bus, hw_plugin_t *p);
extern int hw_plugin_stop(hw_bus_t *bus, hw_plugin_t *p);
extern int hw_plugin_unload(hw_bus_t *bus, hw_plugin_t *p);
extern void hw_plugin_free_meta(hw_plugin_t *p); /* 释放节点动态元数据 */

/* ============================================================
 * 运行时注入（bus/runtime.c）
 * ============================================================ */
extern void hw_runtime_bus_bind(hw_bus_t *bus);
extern void hw_runtime_fill(hw_runtime_t *rt);
extern void hw_plugin_inject_runtime(hw_bus_t *bus, hw_plugin_t *p);
extern hw_bus_t *hw_runtime_bus(void);

/* ============================================================
 * CLI
 * ============================================================ */
extern int hw_bus_cli(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_BUS_H */