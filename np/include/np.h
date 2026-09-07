/*
 * np.h — HWRun OS 网络协议（NP）公共接口
 *
 * 本头文件定义 NP 插件对外暴露的数据结构与接口。
 * 上层插件（CLUSTER / FS_TRANSFER / NODE_DISCOVERY / CONSENSUS / ...）
 * 通过 get_interface("NP") 获取 hw_np_ops_t，从而以统一抽象进行
 * socket 通信与网络管理，不直接操作底层网络栈。
 *
 * 设计原则：协议驱动、统一抽象、真实功能（基于 POSIX socket），
 * 所有函数优雅处理失败（返回 0 成功 / 负 errno），绝不崩溃。
 *
 * 说明：本实现为真实 POSIX 用户态网络操作（socket/bind/... 直通 libc），
 * 与设计文档中"内核模块"叙述的机制等价，但运行于普通用户态。
 */

#ifndef HW_NP_H
#define HW_NP_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * socket 协议族 / 类型常量（与 POSIX 值对齐，便于上层直接使用）
 * ============================================================ */
#define NP_AF_UNIX      AF_UNIX      /* 1  本地域 socket   */
#define NP_AF_INET      AF_INET      /* 2  IPv4            */
#define NP_AF_INET6     AF_INET6     /* 10 IPv6            */

#define NP_SOCK_STREAM  SOCK_STREAM  /* 1  字节流          */
#define NP_SOCK_DGRAM   SOCK_DGRAM   /* 2  数据报          */
#define NP_SOCK_RAW     SOCK_RAW     /* 3  原始套接字      */
#define NP_SOCK_RDM     SOCK_RDM     /* 4  可靠数据报      */
#define NP_SOCK_SEQPACKET SOCK_SEQPACKET /* 5 有序包流    */

#define NP_SHUT_RD      SHUT_RD
#define NP_SHUT_WR      SHUT_WR
#define NP_SHUT_RDWR    SHUT_RDWR

/* ============================================================
 * socket 状态
 * ============================================================ */
typedef enum {
    NP_SOCK_UNCONNECTED = 0,   /* 已创建、已绑定，无连接 */
    NP_SOCK_CONNECTING,        /* 连接中（非阻塞 connect） */
    NP_SOCK_CONNECTED,         /* 已连接（TCP） */
    NP_SOCK_DISCONNECTING,     /* 半关闭/断开中 */
    NP_SOCK_LISTENING,         /* 监听中 */
} np_sock_state_t;

/* ============================================================
 * 连接表条目（描述一个被 NP 跟踪的 socket 描述符）
 * ============================================================ */
typedef struct np_socket {
    int             fd;           /* 内核 socket 文件描述符    */
    int             domain;       /* 协议族 AF_INET 等         */
    int             type;         /* socket 类型 SOCK_STREAM 等 */
    int             protocol;     /* 协议 IPPROTO_TCP/UDP       */
    np_sock_state_t state;        /* 当前状态                  */
    char            local_ip[64];  /* 本地 IP（点分十进制，可为空） */
    unsigned        local_port;   /* 本地端口（主机字节序）    */
    char            remote_ip[64]; /* 对端 IP                  */
    unsigned        remote_port;  /* 对端端口                 */
} np_socket_t;

/* ============================================================
 * 网络接口信息
 * ============================================================ */
typedef struct np_interface {
    char     name[64];       /* 接口名：eth0 / wlan0 / lo */
    char     mac[32];        /* 硬件地址（可为空）        */
    char     ipv4[64];       /* IPv4 地址（点分，可为空） */
    char     netmask[64];    /* IPv4 子网掩码             */
    char     gateway[64];    /* 默认网关（可为空）        */
    char     ipv6[128];      /* IPv6 地址（可为空）       */
    uint32_t mtu;            /* MTU                       */
    int      up;             /* 是否启用                  */
    uint64_t rx_bytes;       /* 接收字节                  */
    uint64_t tx_bytes;       /* 发送字节                  */
    uint64_t rx_packets;     /* 接收包数                  */
    uint64_t tx_packets;     /* 发送包数                  */
} np_interface_t;

/* ============================================================
 * 统计信息
 * ============================================================ */
typedef struct np_stats {
    uint64_t tcp_sockets;    /* 跟踪中的 TCP 连接数   */
    uint64_t udp_sockets;    /* 跟踪中的 UDP socket 数 */
    uint64_t unix_sockets;   /* 跟踪中的 UNIX socket   */
    uint64_t total_sockets;  /* 跟踪中的 socket 总数  */
    uint64_t rx_bytes;       /* 所有接口聚合接收字节  */
    uint64_t tx_bytes;       /* 所有接口聚合发送字节  */
    uint64_t rx_packets;     /* 聚合接收包 */
    uint64_t tx_packets;     /* 聚合发送包 */
    uint64_t iface_count;    /* 检测到的接口数        */
} np_stats_t;

/* ============================================================
 * NP 对外协议接口（get_interface("NP") 返回此结构）
 *
 * 约定：返回 0 表示成功；失败返回对应负 errno（如 -EINVAL、-ECONNREFUSED），
 *      不设置全局 errno，不崩溃。
 * ============================================================ */
typedef struct hw_np_ops {
    /* ---------- socket 生命周期 ---------- */
    /* 创建 socket：成功时 *out_fd = 描述符，返回 0；失败返回负 errno */
    int (*socket)(int domain, int type, int protocol, int *out_fd);
    /* 绑定（IP 字符串 + 端口，port==0 由内核分配） */
    int (*bind)(int fd, const char *ip, unsigned port);
    /* 绑定（完整 sockaddr，支持 UNIX 域等） */
    int (*bind_addr)(int fd, const struct sockaddr *addr, socklen_t len);
    /* 监听 */
    int (*listen)(int fd, int backlog);
    /* 接受连接：成功 *out_fd 为新连接；可选返回对端 ip/port */
    int (*accept)(int fd, int *out_fd, char *remote_ip, size_t ipcap, unsigned *remote_port);
    /* 连接服务器（IP 字符串 + 端口） */
    int (*connect)(int fd, const char *ip, unsigned port);
    /* 连接（完整 sockaddr） */
    int (*connect_addr)(int fd, const struct sockaddr *addr, socklen_t len);
    /* 关闭 */
    int (*close)(int fd);
    /* 半关闭：how 用 NP_SHUT_*（SHUT_RD/WR/RDWR） */
    int (*shutdown)(int fd, int how);

    /* ---------- 数据收发 ---------- */
    /* 发送：成功返回已发送字节数（>=0）；失败返回负 errno */
    ssize_t (*send)(int fd, const void *data, size_t len, int flags);
    /* 接收：成功返回已接收字节数（==0 表示对端关闭）；失败返回负 errno */
    ssize_t (*recv)(int fd, void *buf, size_t len, int flags);
    /* UDP 发送到指定目标 */
    ssize_t (*sendto)(int fd, const void *data, size_t len, int flags,
                      const struct sockaddr *dest, socklen_t addrlen);
    /* UDP 接收并回填发送方地址 */
    ssize_t (*recvfrom)(int fd, void *buf, size_t len, int flags,
                        struct sockaddr *src, socklen_t *addrlen);

    /* ---------- 名称/地址解析 ---------- */
    /* 主机名解析为 sockaddr 列表：经 getaddrinfo，返回 0 成功、*count 为数量 */
    int (*resolve)(const char *host, unsigned port,
                   struct sockaddr_storage *out, int max, int *out_count);
    /* 便捷：主机名 -> 第一个 IPv4 点分字符串 */
    int (*host_to_ip)(const char *host, char *ip, size_t cap);
    /* 本机主机名 */
    int (*gethostname)(char *out, size_t cap);

    /* ---------- 网络接口枚举 ---------- */
    /* 枚举本地接口：返回接口数到 *out_count，存数组（小于等于 cap） */
    int (*get_interfaces)(np_interface_t *ifaces, int cap, int *out_count);

    /* ---------- 连接管理（NP 维护的连接跟踪表） ---------- */
    int (*conn_get)(int fd, np_socket_t *out);                        /* 按 fd 查连接    */
    int (*conn_list)(np_socket_t *arr, int cap, int *out_count);      /* 枚举全部连接    */

    /* ---------- 统计 ---------- */
    int (*get_stats)(np_stats_t *out);
} hw_np_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* HW_NP_H */