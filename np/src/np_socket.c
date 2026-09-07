/*
 * np_socket.c — NP 插件 socket 操作核心
 *
 * 基于真实 POSIX socket 实现：创建/绑定/监听/连接/接受/关闭/半关闭
 * 以及 send/recv/sendto/recvfrom 数据收发。所有失败返回负 errno。
 *
 * 每次 socket/accept/connect 成功都会在连接跟踪表登记/更新元数据，
 * close 时注销，从而提供统一的连接管理视角。
 */

#define _GNU_SOURCE

#include "np.h"
#include "np_conn.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* ============================================================
 * 工具函数
 * ============================================================ */

/* 通过 getsockname/getpeername 回填连接表的本地/对端地址 */
static void conn_refresh_from_fd(int fd, np_socket_t *c) {
    struct sockaddr_storage ss;
    socklen_t len = sizeof(ss);

    char ip[64];
    unsigned port = 0;
    if (getsockname(fd, (struct sockaddr *)&ss, &len) == 0) {
        ip[0] = '\0';
        if (ss.ss_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            port = ntohs(sin->sin_port);
        } else if (ss.ss_family == AF_INET6) {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
            inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
            port = ntohs(sin6->sin6_port);
        }
        np_conn_set_local(c->fd, ip, port);
    }

    len = sizeof(ss);
    if (getpeername(fd, (struct sockaddr *)&ss, &len) == 0) {
        ip[0] = '\0';
        if (ss.ss_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            port = ntohs(sin->sin_port);
        } else if (ss.ss_family == AF_INET6) {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
            inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
            port = ntohs(sin6->sin6_port);
        }
        np_conn_set_addresses(c->fd, ip, port, NULL, 0);
    }
}

/* 把 "IP:端口" 填充为 sockaddr_in；ip 为空/0.0.0.0 视为通配地址 */
static int fill_in4(const char *ip, unsigned port, struct sockaddr_in *sin) {
    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;
    sin->sin_port = htons((uint16_t)port);
    if (!ip || !*ip || strcmp(ip, "0.0.0.0") == 0 || strcmp(ip, "::") == 0) {
        sin->sin_addr.s_addr = htonl(INADDR_ANY);
        return 0;
    }
    if (inet_pton(AF_INET, ip, &sin->sin_addr) != 1) {
        return -EINVAL;
    }
    return 0;
}

/* ============================================================
 * socket 生命周期
 * ============================================================ */

int np_socket_impl(int domain, int type, int protocol, int *out_fd) {
    if (!out_fd) return -EINVAL;
    int fd = socket(domain, type, protocol);
    if (fd < 0) return -errno;

    /* 登记到连接跟踪表 */
    np_socket_t c;
    memset(&c, 0, sizeof(c));
    c.fd = fd;
    c.domain = domain;
    c.type = type;
    c.protocol = protocol;
    c.state = NP_SOCK_UNCONNECTED;
    np_conn_register(&c);

    *out_fd = fd;
    return 0;
}

int np_bind_addr_impl(int fd, const struct sockaddr *addr, socklen_t len) {
    if (!addr || len == 0) return -EINVAL;
    if (bind(fd, addr, len) != 0) return -errno;
    /* 刷新本地地址 */
    struct sockaddr_storage ss;
    socklen_t sl = sizeof(ss);
    if (getsockname(fd, (struct sockaddr *)&ss, &sl) == 0) {
        char ip[64] = {0};
        unsigned port = 0;
        if (ss.ss_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            port = ntohs(sin->sin_port);
        } else if (ss.ss_family == AF_INET6) {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
            inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
            port = ntohs(sin6->sin6_port);
        }
        np_conn_set_local(fd, ip, port);
    }
    return 0;
}

int np_bind_impl(int fd, const char *ip, unsigned port) {
    struct sockaddr_in sin;
    int rc = fill_in4(ip, port, &sin);
    if (rc != 0) return rc;
    return np_bind_addr_impl(fd, (struct sockaddr *)&sin, sizeof(sin));
}

int np_listen_impl(int fd, int backlog) {
    if (listen(fd, backlog) != 0) return -errno;
    np_conn_update_state(fd, NP_SOCK_LISTENING);
    return 0;
}

int np_accept_impl(int fd, int *out_fd, char *remote_ip, size_t ipcap,
                   unsigned *remote_port) {
    if (!out_fd) return -EINVAL;
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    /* 先清空地址，哪怕是 UNIX 域也能安全解析 */
    memset(&sin, 0, sizeof(sin));

    int newfd = accept(fd, (struct sockaddr *)&sin, &len);
    if (newfd < 0) return -errno;

    /* 登记新连接 */
    np_socket_t c;
    memset(&c, 0, sizeof(c));
    c.fd = newfd;
    c.state = NP_SOCK_CONNECTED;
#if defined(_GNU_SOURCE) && defined(SO_DOMAIN)
    {
        int dom = -1;
        socklen_t dl = sizeof(dom);
        if (getsockopt(newfd, SOL_SOCKET, SO_DOMAIN, &dom, &dl) == 0 && dom >= 0) {
            c.domain = dom;
        } else {
            c.domain = sin.sin_family;
        }
    }
#else
    c.domain = sin.sin_family;
#endif
    /* 类型/协议较难直接查询，沿用监听 socket 的作为近似 */
    np_conn_register(&c);

    if (remote_ip || remote_port) {
        char ip[64] = {0};
        unsigned port = 0;
        if ((socklen_t)len >= (socklen_t)sizeof(struct sockaddr_in) && sin.sin_family == AF_INET) {
            inet_ntop(AF_INET, &sin.sin_addr, ip, sizeof(ip));
            port = ntohs(sin.sin_port);
        }
        if (remote_ip && ipcap > 0) {
            strncpy(remote_ip, ip, ipcap - 1);
            remote_ip[ipcap - 1] = '\0';
        }
        if (remote_port) *remote_port = port;
        np_conn_set_addresses(newfd, ip, port, NULL, 0);
    }

    conn_refresh_from_fd(newfd, &c);
    *out_fd = newfd;
    return 0;
}

int np_connect_addr_impl(int fd, const struct sockaddr *addr, socklen_t len) {
    if (!addr || len == 0) return -EINVAL;
    int rc = connect(fd, addr, len);
    int en = (rc == 0) ? 0 : -errno;

    if (en == 0) {
        np_conn_update_state(fd, NP_SOCK_CONNECTED);
        /* 登记对端地址（IPv4） */
        if (addr->sa_family == AF_INET) {
            const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
            char ip[64];
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            np_conn_set_addresses(fd, ip, ntohs(sin->sin_port), NULL, 0);
        }
        np_socket_t c;
        if (np_conn_get(fd, &c) == 0) conn_refresh_from_fd(fd, &c);
        return 0;
    }

    /* 非阻塞 connect 进行中倒不是致命错误，仍在跟踪表中记录 CONNECTING */
    if (en == -EINPROGRESS || en == -EALREADY || en == -EWOULDBLOCK) {
        np_conn_update_state(fd, NP_SOCK_CONNECTING);
    }
    return en;
}

int np_connect_impl(int fd, const char *ip, unsigned port) {
    struct sockaddr_in sin;
    int rc = fill_in4(ip, port, &sin);
    if (rc != 0) return rc;
    return np_connect_addr_impl(fd, (struct sockaddr *)&sin, sizeof(sin));
}

int np_close_impl(int fd) {
    np_conn_unregister(fd);
    if (close(fd) != 0) return -errno;
    return 0;
}

int np_shutdown_impl(int fd, int how) {
    if (shutdown(fd, how) != 0) return -errno;
    if (how == SHUT_RDWR || how == SHUT_RD) {
        np_conn_update_state(fd, NP_SOCK_DISCONNECTING);
    }
    return 0;
}

/* ============================================================
 * 数据收发
 * ============================================================ */

ssize_t np_send_impl(int fd, const void *data, size_t len, int flags) {
    if (len == 0) return 0;
    if (!data) return -EINVAL;
    ssize_t n = send(fd, data, len, flags);
    if (n < 0) return -errno;
    return n;
}

ssize_t np_recv_impl(int fd, void *buf, size_t len, int flags) {
    if (len == 0) return 0;
    if (!buf) return -EINVAL;
    ssize_t n = recv(fd, buf, len, flags);
    if (n < 0) return -errno;
    return n;
}

ssize_t np_sendto_impl(int fd, const void *data, size_t len, int flags,
                       const struct sockaddr *dest, socklen_t addrlen) {
    if (len == 0) return 0;
    if (!data) return -EINVAL;
    ssize_t n = sendto(fd, data, len, flags, dest, addrlen);
    if (n < 0) return -errno;
    return n;
}

ssize_t np_recvfrom_impl(int fd, void *buf, size_t len, int flags,
                         struct sockaddr *src, socklen_t *addrlen) {
    if (len == 0) return 0;
    if (!buf) return -EINVAL;
    ssize_t n = recvfrom(fd, buf, len, flags, src, addrlen);
    if (n < 0) return -errno;
    return n;
}