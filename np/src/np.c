/*
 * np.c — HWRun OS 网络协议（NP）插件主入口
 *
 * 实现 hw_plugin_t 生命周期（init/start/stop/destroy），
 * 并通过 get_interface("NP") 返回 hw_np_ops_t 协议接口。
 *
 * 本插件作为独立 .so 提供给总线 dlopen 加载，导出 hw_plugin_entry()。
 */

#include "hwrun.h"
#include "../include/np.h"
#include "np_conn.h"

#include <errno.h>

/* ---- 各能力模块实现函数（见 np 系列源码） ---- */
int np_socket_impl(int domain, int type, int protocol, int *out_fd);
int np_bind_impl(int fd, const char *ip, unsigned port);
int np_bind_addr_impl(int fd, const struct sockaddr *addr, socklen_t len);
int np_listen_impl(int fd, int backlog);
int np_accept_impl(int fd, int *out_fd, char *remote_ip, size_t ipcap, unsigned *remote_port);
int np_connect_impl(int fd, const char *ip, unsigned port);
int np_connect_addr_impl(int fd, const struct sockaddr *addr, socklen_t len);
int np_close_impl(int fd);
int np_shutdown_impl(int fd, int how);
ssize_t np_send_impl(int fd, const void *data, size_t len, int flags);
ssize_t np_recv_impl(int fd, void *buf, size_t len, int flags);
ssize_t np_sendto_impl(int fd, const void *data, size_t len, int flags,
                       const struct sockaddr *dest, socklen_t addrlen);
ssize_t np_recvfrom_impl(int fd, void *buf, size_t len, int flags,
                         struct sockaddr *src, socklen_t *addrlen);
int np_resolve_impl(const char *host, unsigned port,
                    struct sockaddr_storage *out, int max, int *out_count);
int np_host_to_ip_impl(const char *host, char *ip, size_t cap);
int np_gethostname_impl(char *out, size_t cap);
int np_get_interfaces_impl(np_interface_t *ifaces, int cap, int *out_count);

/* ---- 统计信息：由接口枚举 + 连接表汇总 ---- */
static int np_stats_impl(np_stats_t *out) {
    if (!out) return -EINVAL;
    memset(out, 0, sizeof(*out));

    np_socket_t conns[256];
    int n = 0;
    if (np_conn_list(conns, 256, &n) != 0) n = 0;
    for (int i = 0; i < n; i++) {
        switch (conns[i].domain) {
        case AF_UNIX:   out->unix_sockets++; break;
        default:        /* IPv4/IPv6 按类型区分 */
            if (conns[i].type == SOCK_STREAM) out->tcp_sockets++;
            else if (conns[i].type == SOCK_DGRAM) out->udp_sockets++;
            break;
        }
    }
    out->total_sockets = out->tcp_sockets + out->udp_sockets + out->unix_sockets;

    np_interface_t ifs[128];
    int c = 0;
    if (np_get_interfaces_impl(ifs, 128, &c) == 0) {
        out->iface_count = (uint64_t)c;
        for (int i = 0; i < c; i++) {
            out->rx_bytes   += ifs[i].rx_bytes;
            out->tx_bytes   += ifs[i].tx_bytes;
            out->rx_packets += ifs[i].rx_packets;
            out->tx_packets += ifs[i].tx_packets;
        }
    }
    return 0;
}

/* ---- 协议接口转发（薄封装，供 get_interface 返回） ---- */
static hw_np_ops_t g_np_ops = {
    .socket        = np_socket_impl,
    .bind          = np_bind_impl,
    .bind_addr     = np_bind_addr_impl,
    .listen        = np_listen_impl,
    .accept        = np_accept_impl,
    .connect       = np_connect_impl,
    .connect_addr  = np_connect_addr_impl,
    .close         = np_close_impl,
    .shutdown      = np_shutdown_impl,
    .send          = np_send_impl,
    .recv          = np_recv_impl,
    .sendto        = np_sendto_impl,
    .recvfrom      = np_recvfrom_impl,
    .resolve       = np_resolve_impl,
    .host_to_ip    = np_host_to_ip_impl,
    .gethostname   = np_gethostname_impl,
    .get_interfaces = np_get_interfaces_impl,
    .conn_get      = np_conn_get,
    .conn_list     = np_conn_list,
    .get_stats     = np_stats_impl,
};

/* 插件私有上下文 */
typedef struct np_pdata {
    int started;
} np_pdata_t;

/* ============================================================
 * 生命周期回调
 * ============================================================ */
static int np_init(hw_plugin_t *self) {
    np_pdata_t *pd;
    if (!self) return HWRUN_EINVAL;

    pd = calloc(1, sizeof(np_pdata_t));
    if (!pd) return HWRUN_ENOMEM;
    pd->started = 0;
    self->private_data = pd;

    /* 清空连接跟踪表，避免宿主重用本插件实例时的脏状态 */
    np_conn_clear();
    return HWRUN_OK;
}

static int np_start(hw_plugin_t *self) {
    np_pdata_t *pd = self ? (np_pdata_t *)self->private_data : NULL;
    if (!pd) return HWRUN_ENOTREADY;
    if (pd->started) return HWRUN_OK;

    /* 启动自检：枚举一次接口（失败不影响启动，仅供预热/日志） */
    np_interface_t ifs[8];
    int c = 0;
    np_get_interfaces_impl(ifs, 8, &c);

    pd->started = 1;
    self->state = HWPLUGIN_STARTED;
    return HWRUN_OK;
}

static int np_stop(hw_plugin_t *self) {
    np_pdata_t *pd = self ? (np_pdata_t *)self->private_data : NULL;
    if (!pd) return HWRUN_OK;
    pd->started = 0;
    return HWRUN_OK;
}

static int np_destroy(hw_plugin_t *self) {
    np_pdata_t *pd = self ? (np_pdata_t *)self->private_data : NULL;
    if (pd) free(pd);
    if (self) self->private_data = NULL;
    np_conn_clear();
    return HWRUN_OK;
}

/* 参数变更回调：本插件暂无可配置参数，一律接受 */
static int np_configure(hw_plugin_t *self, const char *key, const char *value) {
    (void)self; (void)key; (void)value;
    return HWRUN_OK;
}

/* get_interface：返回对外提供的协议实现 */
static void *np_get_interface(const char *protocol) {
    if (protocol && strcmp(protocol, HWPROTO_NP) == 0) {
        return &g_np_ops;
    }
    return NULL;
}

static hw_plugin_t g_np_plugin;

/* ============================================================
 * 插件入口：dlopen 加载时调用，返回 hw_plugin_t*
 * ============================================================ */
__attribute__((visibility("default")))
hw_plugin_t *hw_plugin_entry(void) {
    memset(&g_np_plugin, 0, sizeof(g_np_plugin));

    /* 基础元数据（与 plugin.yml 保持一致） */
    strncpy(g_np_plugin.id, "np", sizeof(g_np_plugin.id) - 1);
    strncpy(g_np_plugin.name, "Network Protocol", sizeof(g_np_plugin.name) - 1);
    strncpy(g_np_plugin.version, "1.0.0", sizeof(g_np_plugin.version) - 1);
    g_np_plugin.type = HWPLUGIN_TYPE_NETWORK;
    g_np_plugin.state = HWPLUGIN_INSTALLED;
    strncpy(g_np_plugin.description,
            "HWRun OS 网络协议：socket、TCP/UDP/UNIX、名称解析、网络接口枚举的统一抽象",
            sizeof(g_np_plugin.description) - 1);

    /* 提供的协议 */
    static char *np_provides[] = { "NP" };
    g_np_plugin.provides = np_provides;
    g_np_plugin.provides_count = 1;

    /* 依赖的协议（仅声明，真正校验由总线 && METAPROTO 完成） */
    static char *np_requires[] = {
        "HAP", "LOG", "PARAM", "METAPROTO",
    };
    g_np_plugin.requires = np_requires;
    g_np_plugin.requires_count = 4;

    /* 生命周期 */
    g_np_plugin.ops.init          = np_init;
    g_np_plugin.ops.start         = np_start;
    g_np_plugin.ops.stop          = np_stop;
    g_np_plugin.ops.destroy       = np_destroy;
    g_np_plugin.ops.configure     = np_configure;
    g_np_plugin.ops.get_interface = np_get_interface;

    return &g_np_plugin;
}