/*
 * np_resolve.c — NP 插件名称/地址解析
 *
 * 基于 getaddrinfo 实现主机名解析、加载 IPv4 地址等能力。
 * 所有失败返回负 errno。
 */

#include "np.h"

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int np_resolve_impl(const char *host, unsigned port,
                    struct sockaddr_storage *out, int max, int *out_count) {
    if (!host || !out || !out_count) return -EINVAL;
    if (max <= 0) return -EINVAL;

    struct addrinfo hints;
    struct addrinfo *res = NULL, *ai;
    char serv[32];
    snprintf(serv, sizeof(serv), "%u", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      /* 同时解析 IPv4/IPv6 */
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, serv, &hints, &res);
    if (rc != 0) return -EINVAL;      /* EAI_* 与 errno 不通用，统一映射 */

    int n = 0;
    for (ai = res; ai && n < max; ai = ai->ai_next) {
        if ((socklen_t)ai->ai_addrlen <= (socklen_t)sizeof(struct sockaddr_storage)) {
            memcpy(&out[n], ai->ai_addr, ai->ai_addrlen);
            n++;
        }
    }
    freeaddrinfo(res);
    *out_count = n;
    return 0;
}

int np_host_to_ip_impl(const char *host, char *ip, size_t cap) {
    if (!host || !ip || cap == 0) return -EINVAL;
    ip[0] = '\0';

    struct addrinfo hints;
    struct addrinfo *res = NULL, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, NULL, &hints, &res) != 0) return -EINVAL;

    int found = 0;
    for (ai = res; ai; ai = ai->ai_next) {
        if (ai->ai_family == AF_INET) {
            const struct sockaddr_in *sin = (const struct sockaddr_in *)ai->ai_addr;
            if (inet_ntop(AF_INET, &sin->sin_addr, ip, cap)) {
                found = 1;
                break;
            }
        }
    }
    freeaddrinfo(res);
    if (!found) return -ENOENT;
    return 0;
}

int np_gethostname_impl(char *out, size_t cap) {
    if (!out || cap == 0) return -EINVAL;
    if (gethostname(out, cap) != 0) {
        out[0] = '\0';
        return -errno;
    }
    out[cap - 1] = '\0';
    return 0;
}