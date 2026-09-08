/*
 * metaproto.h — METAPROTO（元协议）协议接口
 *
 * 协议的协议：负责协议注册、发现、解析、版本管理、订阅通知。
 * 依赖链第 1 环。BUS 启动时最先自举本子系统。
 */

#ifndef HWRUN_METAPROTO_H
#define HWRUN_METAPROTO_H

#include "hwrun.h"
#include "hwlock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 注册表条目状态 */
enum {
    HWPROTO_STATE_REGISTERED = 1,   /* 已注册，提供者启动 */
    HWPROTO_STATE_REMOVED    = 2,   /* 已注销           */
    HWPROTO_STATE_CHANGED    = 3,   /* 版本/提供者变化  */
};

/* ============================================================
 * METAPROTO 公共接口（作为协议对外暴露）
 * ============================================================ */
typedef struct hw_metaproto_api {
    int (*register_protocol)(const char *protocol, const char *version,
                             const char *plugin_id, void *implementation);
    int (*unregister_protocol)(const char *protocol, const char *plugin_id);
    int (*resolve)(const char *protocol, const char *version_req,
                   hw_protocol_route_t **out);
    int (*list)(hw_protocol_route_t ***out, int *count);
    int (*subscribe)(const char *plugin_id, const char *protocol,
                     void (*cb)(const char *p, const char *v,
                                const char *plugin, int state));
    int (*publish_notify)(const char *protocol, const char *version,
                          const char *plugin_id, int state);
    int (*check_deps)(const char *const *requires, int count,
                      char *missing_protocol, size_t cap);
} hw_metaproto_api_t;

/* ============================================================
 * METAPROTO 全局注册表（由 bus 自举时初始化）
 * ============================================================ */
typedef struct hw_metaproto_registry {
    hw_protocol_route_t *routes;          /* 路由表链表    */
    hw_protocol_sub_t   *subscribers;     /* 订阅者链表    */
    hw_locker_t          lock;            /* HWLOCK_RW：保护 routes/subscribers */
    char                 state_file[256]; /* 持久化路径   */
    int                  initialized;
} hw_metaproto_registry_t;

/* ============================================================
 * 实现函数（bus/metaproto.c 提供）
 * ============================================================ */
extern int  hw_metaproto_init(hw_metaproto_registry_t *reg,
                              const char *state_dir);
extern void hw_metaproto_shutdown(hw_metaproto_registry_t *reg);

extern int  hw_metaproto_register(hw_metaproto_registry_t *reg,
                                  const char *protocol, const char *version,
                                  const char *plugin_id, void *implementation);
extern int  hw_metaproto_unregister(hw_metaproto_registry_t *reg,
                                    const char *protocol, const char *plugin_id);
extern int  hw_metaproto_resolve(hw_metaproto_registry_t *reg,
                                 const char *protocol, const char *version_req,
                                 hw_protocol_route_t **out);
extern int  hw_metaproto_list(hw_metaproto_registry_t *reg,
                              hw_protocol_route_t ***out, int *count);
extern int  hw_metaproto_subscribe(hw_metaproto_registry_t *reg,
                                   const char *plugin_id, const char *protocol,
                                   void (*cb)(const char *p, const char *v,
                                              const char *plugin, int state));
extern void hw_metaproto_notify(hw_metaproto_registry_t *reg,
                                const char *protocol, const char *version,
                                const char *plugin_id, int state);
extern int  hw_metaproto_check_deps(hw_metaproto_registry_t *reg,
                                    const char *const *requires, int count,
                                    char *missing, size_t cap);

/* 版本兼容判断：request "1.0" vs have "1.0" */
extern int  hw_proto_version_compat(const char *have, const char *req);
extern int  hw_proto_version_cmp(const char *a, const char *b);

/* 导出 METAPROTO api（供插件 get_interface 取用） */
extern void hw_metaproto_export_api(hw_metaproto_api_t *api,
                                    hw_metaproto_registry_t *reg);

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_METAPROTO_H */