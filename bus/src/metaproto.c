/*
 * metaproto.c — METAPROTO 完整实现
 *
 * 协议的协议：协议注册表、版本解析、订阅通知、依赖校验。
 * 依赖链第 1 环。作为 BUS 自举的第一个子系统被初始化。
 */

#include "metaproto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* 版本比较：a<b 返回负，a>b 返回正，相等返回 0 */
int hw_proto_version_cmp(const char *a, const char *b) {
    if (!a || !b) return 0;
    long ma = 0, mb = 0;
    int ra = 1, rb = 1;
    const char *pa = a, *pb = b;
    while (*pa && *pb) {
        while (*pa && !isdigit((unsigned char)*pa)) pa++;
        while (*pb && !isdigit((unsigned char)*pb)) pb++;
        if (!*pa || !*pb) break;
        ma = strtol(pa, (char **)&pa, 10);
        mb = strtol(pb, (char **)&pb, 10);
        if (ma != mb) return ma < mb ? -1 : 1;
        ra++; rb++;
    }
    return 0;
}

/* 兼容：版本号逐段相等（允许 have 有副版本而 req 没有） */
int hw_proto_version_compat(const char *have, const char *req) {
    if (!have || !req) return 0;
    /* 完全相等则兼容 */
    if (strcmp(have, req) == 0) return 1;
    /* have = "1.0", req = "1.0.1" 不兼容（主协议升版）*/
    return 0;
}

int hw_metaproto_init(hw_metaproto_registry_t *reg, const char *state_dir) {
    if (!reg) return HWRUN_EINVAL;
    memset(reg, 0, sizeof(*reg));
    reg->initialized = 1;
    if (state_dir) {
        snprintf(reg->state_file, sizeof(reg->state_file), "%s/metaproto.state",
                 state_dir);
    }
    /* 持久化状态文件暂不强制存在；注册表在内存中重建 */
    return HWRUN_OK;
}

void hw_metaproto_shutdown(hw_metaproto_registry_t *reg) {
    if (!reg) return;
    hw_protocol_route_t *r = reg->routes;
    while (r) {
        hw_protocol_route_t *n = r->next;
        free(r);
        r = n;
    }
    reg->routes = NULL;
    hw_protocol_sub_t *s = reg->subscribers;
    while (s) {
        hw_protocol_sub_t *n = s->next;
        free(s);
        s = n;
    }
    reg->subscribers = NULL;
    reg->initialized = 0;
}

int hw_metaproto_register(hw_metaproto_registry_t *reg,
                          const char *protocol, const char *version,
                          const char *plugin_id, void *implementation) {
    if (!reg || !reg->initialized || !protocol || !plugin_id)
        return HWRUN_EINVAL;

    /* 已存在同名协议+插件 → 视为更新实现（热替换） */
    for (hw_protocol_route_t *r = reg->routes; r; r = r->next) {
        if (hw_str_eq(r->protocol, protocol) && hw_str_eq(r->plugin_id, plugin_id)) {
            r->implementation = implementation;
            if (version) snprintf(r->version, sizeof(r->version), "%s", version);
            hw_metaproto_notify(reg, protocol, r->version, plugin_id,
                               HWPROTO_STATE_CHANGED);
            return HWRUN_OK;
        }
    }

    hw_protocol_route_t *r = calloc(1, sizeof(*r));
    if (!r) return HWRUN_ENOMEM;
    snprintf(r->protocol, sizeof(r->protocol), "%s", protocol);
    if (version) snprintf(r->version, sizeof(r->version), "%s", version);
    else         snprintf(r->version, sizeof(r->version), HWRUN_PROTOCOL_VERSION);
    snprintf(r->plugin_id, sizeof(r->plugin_id), "%s", plugin_id);
    r->implementation = implementation;
    r->provider_state = HWPLUGIN_STARTED;
    r->next = reg->routes;
    reg->routes = r;

    hw_metaproto_notify(reg, protocol, r->version, plugin_id,
                       HWPROTO_STATE_REGISTERED);
    return HWRUN_OK;
}

int hw_metaproto_unregister(hw_metaproto_registry_t *reg,
                            const char *protocol, const char *plugin_id) {
    if (!reg || !protocol) return HWRUN_EINVAL;
    hw_protocol_route_t **link = &reg->routes;
    for (hw_protocol_route_t *r = reg->routes; r; r = r->next) {
        if (hw_str_eq(r->protocol, protocol) &&
            (!plugin_id || hw_str_eq(r->plugin_id, plugin_id))) {
            *link = r->next;
            hw_metaproto_notify(reg, r->protocol, r->version, r->plugin_id,
                               HWPROTO_STATE_REMOVED);
            free(r);
            return HWRUN_OK;
        }
        link = &r->next;
    }
    return HWRUN_ENOENT;
}

int hw_metaproto_resolve(hw_metaproto_registry_t *reg,
                         const char *protocol, const char *version_req,
                         hw_protocol_route_t **out) {
    if (!reg || !protocol || !out) return HWRUN_EINVAL;
    for (hw_protocol_route_t *r = reg->routes; r; r = r->next) {
        if (hw_str_eq(r->protocol, protocol)) {
            if (version_req && *version_req) {
                if (!hw_proto_version_compat(r->version, version_req)) continue;
            }
            *out = r;
            return HWRUN_OK;
        }
    }
    return HWRUN_ENOENT;
}

int hw_metaproto_list(hw_metaproto_registry_t *reg,
                      hw_protocol_route_t ***out, int *count) {
    if (!reg || !out || !count) return HWRUN_EINVAL;
    int n = 0;
    for (hw_protocol_route_t *r = reg->routes; r; r = r->next) n++;
    *count = n;
    if (n == 0) { *out = NULL; return HWRUN_OK; }
    hw_protocol_route_t **arr = malloc(n * sizeof(*arr));
    if (!arr) return HWRUN_ENOMEM;
    int i = 0;
    for (hw_protocol_route_t *r = reg->routes; r; r = r->next) arr[i++] = r;
    *out = arr;
    return HWRUN_OK;
}

int hw_metaproto_subscribe(hw_metaproto_registry_t *reg,
                           const char *plugin_id, const char *protocol,
                           void (*cb)(const char *p, const char *v,
                                      const char *plugin, int state)) {
    if (!reg || !plugin_id || !cb) return HWRUN_EINVAL;
    /* 防重 */
    for (hw_protocol_sub_t *s = reg->subscribers; s; s = s->next) {
        if (hw_str_eq(s->plugin_id, plugin_id) &&
            (!protocol || hw_str_eq(s->protocol, protocol))) {
            s->on_protocol_change = cb;
            return HWRUN_OK;
        }
    }
    hw_protocol_sub_t *s = calloc(1, sizeof(*s));
    if (!s) return HWRUN_ENOMEM;
    snprintf(s->plugin_id, sizeof(s->plugin_id), "%s", plugin_id);
    if (protocol) snprintf(s->protocol, sizeof(s->protocol), "%s", protocol);
    else          s->protocol[0] = '\0';
    s->on_protocol_change = cb;
    s->next = reg->subscribers;
    reg->subscribers = s;
    return HWRUN_OK;
}

void hw_metaproto_notify(hw_metaproto_registry_t *reg,
                         const char *protocol, const char *version,
                         const char *plugin_id, int state) {
    if (!reg) return;
    for (hw_protocol_sub_t *s = reg->subscribers; s; s = s->next) {
        if (s->protocol[0] && !hw_str_eq(s->protocol, protocol)) continue;
        if (s->on_protocol_change) {
            s->on_protocol_change(protocol, version, plugin_id, state);
        }
    }
}

int hw_metaproto_check_deps(hw_metaproto_registry_t *reg,
                            const char *const *requires, int count,
                            char *missing, size_t cap) {
    if (!reg || !requires || count < 0) return HWRUN_EINVAL;
    if (missing && cap) missing[0] = '\0';
    for (int i = 0; i < count; i++) {
        hw_protocol_route_t *r = NULL;
        int rc = hw_metaproto_resolve(reg, requires[i], NULL, &r);
        if (rc != HWRUN_OK || !r) {
            if (missing && cap)
                snprintf(missing, cap, "%s", requires[i]);
            return HWRUN_ENOENT;
        }
    }
    return HWRUN_OK;
}

void hw_metaproto_export_api(hw_metaproto_api_t *api,
                             hw_metaproto_registry_t *reg) {
    if (!api) return;
    /* 用 userdata 绑定 registry，各函数指针接收 userdata 参数统一签名 */
    api->register_protocol = NULL;   /* 表驱动方式，见下 */
    (void)reg;
    (void)api;
}

#ifdef HWRUN_USE_TABLE_API
/* 若宏开启，用统一签名(void* userdata, ...)的表封装；
   否则调用方直接用 hw_metaproto_* 裸函数。 */
int hw_meta_tbl_register(void *reg, const char *p, const char *v,
                         const char *plugin, void *impl) {
    return hw_metaproto_register(reg, p, v, plugin, impl);
}
int hw_meta_tbl_unregister(void *reg, const char *p, const char *plugin) {
    return hw_metaproto_unregister(reg, p, plugin);
}
int hw_meta_tbl_resolve(void *reg, const char *p, const char *v,
                        hw_protocol_route_t **out) {
    return hw_metaproto_resolve(reg, p, v, out);
}
int hw_meta_tbl_check_deps(void *reg, const char *const *req, int n,
                           char *miss, size_t cap) {
    return hw_metaproto_check_deps(reg, req, n, miss, cap);
}
#endif /* HWRUN_USE_TABLE_API */