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
        while (*pa && !isdigit((unsigned char)*pa))
            pa++;
        while (*pb && !isdigit((unsigned char)*pb))
            pb++;
        if (!*pa || !*pb) break;
        ma = strtol(pa, (char **)&pa, 10);
        mb = strtol(pb, (char **)&pb, 10);
        if (ma != mb) return ma < mb ? -1 : 1;
        ra++;
        rb++;
    }
    return 0;
}

/* 解析 "major[.minor[.patch]]" 各段；缺失段记为 0，返回已识别的段数 */
static int version_segments(const char *v, long *major, long *minor, long *patch) {
    *major = *minor = *patch = 0;
    if (!v || !*v) return 0;
    char *end = NULL;
    *major = strtol(v, &end, 10);
    if (end == v) return 0; /* 首段非数字 */
    int n = 1;
    if (*end == '.') {
        *minor = strtol(end + 1, &end, 10);
        n = 2;
        if (*end == '.') {
            *patch = strtol(end + 1, &end, 10);
            n = 3;
        }
    }
    return n;
}

/* 兼容：语义版本规则。主版本（major）必须相同；req 的 minor <= have 的 minor。
 * 如 have="1.3", req="1.0" 兼容；have="1.0", req="1.3" 不兼容；
 * have="2.0", req="1.9" 主版本不同不兼容。patch 段遵循 semver 语义，
 * 请求方 patch 缺省视为 0，故 have="1.0.1", req="1.0" 兼容。 */
int hw_proto_version_compat(const char *have, const char *req) {
    if (!have || !req) return 0;
    long hma = 0, hmi = 0, hpa = 0, rma = 0, rmi = 0, rpa = 0;
    int hn = version_segments(have, &hma, &hmi, &hpa);
    int rn = version_segments(req, &rma, &rmi, &rpa);
    if (hn == 0 || rn == 0) return 0; /* 任一无法解析则不兼容 */

    /* 主版本必须一致（协议升主版视为破坏性变更，不兼容） */
    if (hma != rma) return 0;
    /* 次版本：have 必须 >= req */
    if (hmi < rmi) return 0;
    /* 同 minor 下 patch：have >= req */
    if (hmi == rmi && hpa < rpa) return 0;
    return 1;
}

int hw_metaproto_init(hw_metaproto_registry_t *reg, const char *state_dir) {
    if (!reg) return HWRUN_EINVAL;
    memset(reg, 0, sizeof(*reg));
    hw_locker_init(&reg->lock, HWLOCK_RW);
    reg->initialized = 1;
    if (state_dir) {
        snprintf(reg->state_file, sizeof(reg->state_file), "%s/metaproto.state", state_dir);
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
    hw_locker_destroy(&reg->lock);
}

int hw_metaproto_register(hw_metaproto_registry_t *reg, const char *protocol, const char *version,
                          const char *plugin_id, void *implementation) {
    if (!reg || !reg->initialized || !protocol || !plugin_id) return HWRUN_EINVAL;

    int rc = HWRUN_OK;
    /* 出锁后派发的通知参数（锁内把要通知的内容快照到局部副本） */
    int notify_state = 0;
    char notify_proto[64] = "", notify_version[16] = "";

    HW_WRLOCK_GUARD(&reg->lock) {
        int updated = 0;
        /* 已存在同名协议+插件 → 视为更新实现（热替换） */
        for (hw_protocol_route_t *r = reg->routes; r; r = r->next) {
            if (hw_str_eq(r->protocol, protocol) && hw_str_eq(r->plugin_id, plugin_id)) {
                r->implementation = implementation;
                if (version) snprintf(r->version, sizeof(r->version), "%s", version);
                snprintf(notify_proto, sizeof(notify_proto), "%s", r->protocol);
                snprintf(notify_version, sizeof(notify_version), "%s", r->version);
                notify_state = HWPROTO_STATE_CHANGED;
                updated = 1;
                break;
            }
        }
        if (!updated) {
            hw_protocol_route_t *r = calloc(1, sizeof(*r));
            if (!r) {
                rc = HWRUN_ENOMEM;
            } else {
                snprintf(r->protocol, sizeof(r->protocol), "%s", protocol);
                if (version)
                    snprintf(r->version, sizeof(r->version), "%s", version);
                else
                    snprintf(r->version, sizeof(r->version), HWRUN_PROTOCOL_VERSION);
                snprintf(r->plugin_id, sizeof(r->plugin_id), "%s", plugin_id);
                r->implementation = implementation;
                r->provider_state = HWPLUGIN_STARTED;
                r->next = reg->routes;
                reg->routes = r;
                snprintf(notify_proto, sizeof(notify_proto), "%s", protocol);
                snprintf(notify_version, sizeof(notify_version), "%s", r->version);
                notify_state = HWPROTO_STATE_REGISTERED;
            }
        }
    }

    /* 出锁后派发：持锁期间绝不调用 subscriber 回调 */
    if (rc == HWRUN_OK && notify_state)
        hw_metaproto_notify(reg, notify_proto, notify_version, plugin_id, notify_state);
    return rc;
}

int hw_metaproto_unregister(hw_metaproto_registry_t *reg, const char *protocol,
                            const char *plugin_id) {
    if (!reg || !protocol) return HWRUN_EINVAL;
    int rc = HWRUN_ENOENT;
    int notify_state = 0;
    char notify_proto[64] = "", notify_version[16] = "", notify_plugin[64] = "";

    HW_WRLOCK_GUARD(&reg->lock) {
        hw_protocol_route_t **link = &reg->routes;
        for (hw_protocol_route_t *r = reg->routes; r; r = r->next) {
            if (hw_str_eq(r->protocol, protocol) &&
                (!plugin_id || hw_str_eq(r->plugin_id, plugin_id))) {
                *link = r->next;
                snprintf(notify_proto, sizeof(notify_proto), "%s", r->protocol);
                snprintf(notify_version, sizeof(notify_version), "%s", r->version);
                snprintf(notify_plugin, sizeof(notify_plugin), "%s", r->plugin_id);
                notify_state = HWPROTO_STATE_REMOVED;
                free(r);
                rc = HWRUN_OK;
                break;
            }
            link = &r->next;
        }
    }

    /* 出锁后派发：持锁期间绝不调用 subscriber 回调 */
    if (rc == HWRUN_OK)
        hw_metaproto_notify(reg, notify_proto, notify_version, notify_plugin, notify_state);
    return rc;
}

int hw_metaproto_resolve(hw_metaproto_registry_t *reg, const char *protocol,
                         const char *version_req, hw_protocol_route_t **out) {
    if (!reg || !protocol || !out) return HWRUN_EINVAL;
    int rc = HWRUN_ENOENT;
    HW_RDLOCK_GUARD(&reg->lock) {
        for (hw_protocol_route_t *r = reg->routes; r; r = r->next) {
            if (hw_str_eq(r->protocol, protocol)) {
                if (version_req && *version_req) {
                    if (!hw_proto_version_compat(r->version, version_req)) continue;
                }
                *out = r; /* 借用指针：调用方自行保证与 unregister 不同步竞争 */
                rc = HWRUN_OK;
                break;
            }
        }
    }
    return rc;
}

int hw_metaproto_list(hw_metaproto_registry_t *reg, hw_protocol_route_t ***out, int *count) {
    if (!reg || !out || !count) return HWRUN_EINVAL;
    int rc = HWRUN_OK;
    HW_RDLOCK_GUARD(&reg->lock) {
        int n = 0;
        for (hw_protocol_route_t *r = reg->routes; r; r = r->next)
            n++;
        *count = n;
        if (n == 0) {
            *out = NULL;
        } else {
            hw_protocol_route_t **arr = malloc(n * sizeof(*arr));
            if (!arr) {
                rc = HWRUN_ENOMEM;
            } else {
                int i = 0;
                for (hw_protocol_route_t *r = reg->routes; r; r = r->next)
                    arr[i++] = r;
                *out = arr;
            }
        }
    }
    return rc;
}

int hw_metaproto_subscribe(hw_metaproto_registry_t *reg, const char *plugin_id,
                           const char *protocol,
                           void (*cb)(const char *p, const char *v, const char *plugin,
                                      int state)) {
    if (!reg || !plugin_id || !cb) return HWRUN_EINVAL;
    int rc = HWRUN_OK;
    int found = 0;
    HW_WRLOCK_GUARD(&reg->lock) {
        /* 防重 */
        for (hw_protocol_sub_t *s = reg->subscribers; s; s = s->next) {
            if (hw_str_eq(s->plugin_id, plugin_id) &&
                (!protocol || hw_str_eq(s->protocol, protocol))) {
                s->on_protocol_change = cb;
                found = 1;
                break;
            }
        }
        if (!found) {
            hw_protocol_sub_t *s = calloc(1, sizeof(*s));
            if (!s) {
                rc = HWRUN_ENOMEM;
            } else {
                snprintf(s->plugin_id, sizeof(s->plugin_id), "%s", plugin_id);
                if (protocol)
                    snprintf(s->protocol, sizeof(s->protocol), "%s", protocol);
                else
                    s->protocol[0] = '\0';
                s->on_protocol_change = cb;
                s->next = reg->subscribers;
                reg->subscribers = s;
            }
        }
    }
    return rc;
}

void hw_metaproto_notify(hw_metaproto_registry_t *reg, const char *protocol, const char *version,
                         const char *plugin_id, int state) {
    if (!reg) return;
    /* 锁内快照匹配订阅者的回调指针（subscriber 可能被并发 unsubscribe/free），
     * 出锁后逐个派发 —— 持锁期间绝不调用 subscriber 回调。
     * 快照上限 64：超限静默截断（订阅者数百的场景不存在）。 */
    typedef void (*sub_cb_t)(const char *, const char *, const char *, int);
    sub_cb_t snaps[64];
    int n = 0;
    HW_RDLOCK_GUARD(&reg->lock) {
        for (hw_protocol_sub_t *s = reg->subscribers; s; s = s->next) {
            if (s->protocol[0] && !hw_str_eq(s->protocol, protocol)) continue;
            if (n < (int)(sizeof(snaps) / sizeof(snaps[0]))) snaps[n++] = s->on_protocol_change;
        }
    }
    for (int i = 0; i < n; i++) {
        if (snaps[i]) snaps[i](protocol, version, plugin_id, state);
    }
}

int hw_metaproto_check_deps(hw_metaproto_registry_t *reg, const char *const *requires, int count,
                            char *missing, size_t cap) {
    if (!reg || !requires || count < 0) return HWRUN_EINVAL;
    if (missing && cap) missing[0] = '\0';
    int rc = HWRUN_OK;
    /* RDLOCK 内直接查表（不复用 resolve 以避免对同一把读写锁嵌套读加锁） */
    HW_RDLOCK_GUARD(&reg->lock) {
        for (int i = 0; i < count; i++) {
            int ok = 0;
            for (hw_protocol_route_t *r = reg->routes; r; r = r->next) {
                if (hw_str_eq(r->protocol, requires[i])) {
                    ok = 1;
                    break;
                }
            }
            if (!ok) {
                if (missing && cap) snprintf(missing, cap, "%s", requires[i]);
                rc = HWRUN_ENOENT;
                break;
            }
        }
    }
    return rc;
}