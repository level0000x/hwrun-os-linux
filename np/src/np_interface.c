/*
 * np_interface.c — NP 插件网络接口枚举
 *
 * 枚举本地网络接口并获取 ip / mac / mtu / 状态 / 收发统计。
 * 数据来源：getifaddrs()（IP/掩码/标志）+ /sys/class/net 目录（mac/mtu/carrier）
 *          + /proc/net/dev（收发统计）+ /proc/net/route（默认网关）。
 * 任何一项读取失败都优雅降级（字段留空/0），绝不崩溃。
 */

#include "np.h"

#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

/* 读取一个文本文件并去尾空白写入 out（失败留空） */
static void read_txt(const char *path, char *out, size_t cap) {
    out[0] = '\0';
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    ssize_t n = read(fd, out, cap - 1);
    close(fd);
    if (n < 0) n = 0;
    out[n] = '\0';
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' '))
        out[--n] = '\0';
}

/* 在已收集的接口数组中查找 name，找不到返回 -1 */
static int find_name(const np_interface_t *arr, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(arr[i].name, name) == 0) return i;
    }
    return -1;
}

/* 解析 /proc/net/dev 中某接口的收发统计 */
static void proc_dev_stats(const char *name, np_interface_t *o) {
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) return;
    char line[640];
    /* 跳过两行表头 */
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return;
    }
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        size_t nl = (size_t)(colon - line);
        if (nl > IFNAMSIZ) nl = IFNAMSIZ;
        char ifn[IFNAMSIZ + 1];
        memcpy(ifn, line, nl);
        ifn[nl] = '\0';
        while (nl > 0 && ifn[nl - 1] == ' ')
            ifn[--nl] = '\0';
        if (strcmp(ifn, name) != 0) continue;

        /* 读取头 16 个数字：0-7 接收，8-15 发送 */
        unsigned long long nums[16] = {0};
        char *p = colon + 1;
        int c = 0;
        while (c < 16) {
            while (*p == ' ')
                p++;
            if (!*p) break;
            if (sscanf(p, "%llu", &nums[c]) == 1) {
                c++;
                while (*p && *p != ' ')
                    p++;
            } else
                break;
        }
        if (c >= 1) o->rx_bytes = nums[0];
        if (c >= 2) o->rx_packets = nums[1];
        if (c >= 9) o->tx_bytes = nums[8];
        if (c >= 10) o->tx_packets = nums[9];
        break;
    }
    fclose(fp);
}

/* 解析 /proc/net/route 得到默认网关，写入 gw（失败置空） */
static void proc_default_gateway(char *gw, size_t cap) {
    gw[0] = '\0';
    FILE *fp = fopen("/proc/net/route", "r");
    if (!fp) return;
    char line[512];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return;
    } /* 跳过表头 */
    while (fgets(line, sizeof(line), fp)) {
        char iface[IFNAMSIZ + 1];
        unsigned long dest = 0, gateway = 0, mask = 0, metric = 0;
        if (sscanf(line, "%15s %lx %lx %*x %*d %*d %lu %lx", iface, &dest, &gateway, &metric,
                   &mask) == 5) {
            /* 0.0.0.0 目标即默认路由 */
            if (dest == 0 && gateway != 0) {
                uint32_t g = ntohl((uint32_t)gateway);
                struct in_addr ia;
                ia.s_addr = g;
                inet_ntop(AF_INET, &ia, gw, cap);
                break;
            }
        }
    }
    fclose(fp);
}

int np_get_interfaces_impl(np_interface_t *ifaces, int cap, int *out_count) {
    if (!ifaces || !out_count) return -EINVAL;
    if (cap <= 0) return -EINVAL;
    *out_count = 0;

    struct ifaddrs *ifa0 = NULL;
    if (getifaddrs(&ifa0) != 0) return -errno;

    /* 第一遍：收集接口名并填充 IPv4/IPv6 地址与掩码 */
    for (struct ifaddrs *ifa = ifa0; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name) continue;
        if (!ifa->ifa_addr) continue;

        int idx = find_name(ifaces, *out_count, ifa->ifa_name);
        if (idx < 0) {
            if (*out_count >= cap) continue;
            idx = *out_count;
            memset(&ifaces[idx], 0, sizeof(ifaces[idx]));
            strncpy(ifaces[idx].name, ifa->ifa_name, sizeof(ifaces[idx].name) - 1);
            ifaces[idx].up = (ifa->ifa_flags & IFF_UP) ? 1 : 0;
            (*out_count)++;
        }

        if (ifa->ifa_addr->sa_family == AF_INET) {
            const struct sockaddr_in *sin = (const struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &sin->sin_addr, ifaces[idx].ipv4, sizeof(ifaces[idx].ipv4));
            if (ifa->ifa_netmask) {
                const struct sockaddr_in *sn = (const struct sockaddr_in *)ifa->ifa_netmask;
                inet_ntop(AF_INET, &sn->sin_addr, ifaces[idx].netmask, sizeof(ifaces[idx].netmask));
            }
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)ifa->ifa_addr;
            inet_ntop(AF_INET6, &sin6->sin6_addr, ifaces[idx].ipv6, sizeof(ifaces[idx].ipv6));
            /* 去掉 scoped 地址的 %iface 后缀 */
            char *pct = strchr(ifaces[idx].ipv6, '%');
            if (pct) *pct = '\0';
        }
    }

    /* 第二遍：补充 mac / mtu / carrier / 统计（来自 /sys/class/net + /proc/net/dev） */
    for (int i = 0; i < *out_count; i++) {
        char path[256];
        char buf[256];

        snprintf(path, sizeof(path), "/sys/class/net/%s/address", ifaces[i].name);
        read_txt(path, ifaces[i].mac, sizeof(ifaces[i].mac));

        snprintf(path, sizeof(path), "/sys/class/net/%s/mtu", ifaces[i].name);
        read_txt(path, buf, sizeof(buf));
        if (*buf) ifaces[i].mtu = (uint32_t)strtoul(buf, NULL, 10);

        snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", ifaces[i].name);
        read_txt(path, buf, sizeof(buf));
        if (*buf) ifaces[i].up = (strncmp(buf, "up", 2) == 0) ? 1 : ifaces[i].up;

        proc_dev_stats(ifaces[i].name, &ifaces[i]);
    }

    /* 默认网关（IPv4）：若 /proc/net/route 可读则填写到第一个有 IPv4 的接口 */
    {
        typedef char gw_buf[64];
        gw_buf gw;
        proc_default_gateway(gw, sizeof(gw));
        if (*gw) {
            for (int i = 0; i < *out_count; i++) {
                if (ifaces[i].ipv4[0]) {
                    snprintf(ifaces[i].gateway, sizeof(ifaces[i].gateway), "%s", gw);
                    break;
                }
            }
        }
    }

    freeifaddrs(ifa0);
    return 0;
}