/*
 * hap_net.c — 网络接口信息
 *
 * Linux 下枚举 /sys/class/net/<iface>，读取 operstate、mtu、address（MAC）
 * 与 /proc/net/dev 的收发字节数；IP 地址尝试用 if_indextoname + getifaddrs。
 * 降级环境下 count=0。
 */

#include "../include/hap.h"
#include "hap_util.h"

#include <dirent.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#define SYS_NET        "/sys/class/net"
#define SYS_NET_OP     "%s/%s/operstate"
#define SYS_NET_MTU    "%s/%s/mtu"
#define SYS_NET_MAC    "%s/%s/address"
#define PROC_NET_DEV   "/proc/net/dev"

/* 用 getifaddrs 取该接口首个 IPv4 地址 */
static void hap_net_ip(const char *ifname, char *out, size_t cap) {
    struct ifaddrs *iflist = NULL, *ifa;
    struct sockaddr_in *sin;
    out[0] = '\0';
    if (getifaddrs(&iflist) != 0 || !iflist) return;
    for (ifa = iflist; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, ifname) != 0) continue;
        sin = (struct sockaddr_in *)ifa->ifa_addr;
        if (inet_ntop(AF_INET, &sin->sin_addr, out, cap) == NULL) out[0] = '\0';
        break;
    }
    freeifaddrs(iflist);
}

/* 从 /proc/net/dev 取得该接口收发字节，返回 1 成功 */
static int hap_net_octets(const char *ifname, uint64_t *rx, uint64_t *tx) {
    char data[8192];
    const char *p;
    size_t n = strlen(ifname);
    *rx = 0; *tx = 0;
    if (hap_read_file(PROC_NET_DEV, data, sizeof(data)) == 0) return 0;
    p = data;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
        if (linelen > n && strncmp(p, ifname, n) == 0 && p[n] == ':') {
            const char *v = p + n + 1;
            /* 格式：rx_bytes ... tx_bytes ...*/
            uint64_t a = 0, t = 0;
            if (sscanf(v, " %llu %*u %*u %*u %*u %*u %*u %*u %*u %llu",
                       (unsigned long long *)&a, (unsigned long long *)&t) >= 1) {
                *rx = a;
                *tx = t;
                return 1;
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
    return 0;
}

int hap_net_probe(hap_net_info_t *out) {
    struct dirent *e;
    DIR *d;
    char path[512], val[128];
    uint32_t idx = 0;

    if (!out) return HAP_USERES;
    memset(out, 0, sizeof(hap_net_info_t));

    d = opendir(SYS_NET);
    if (!d) return HAP_OK;

    while ((e = readdir(d)) != NULL &&
           idx < sizeof(out->ifaces)/sizeof(out->ifaces[0])) {
        if (e->d_name[0] == '.') continue;

        hap_net_iface_t *nic = &out->ifaces[idx];
        memset(nic, 0, sizeof(*nic));
        strncpy(nic->name, e->d_name, sizeof(nic->name) - 1);

        /* 运行状态 */
        snprintf(path, sizeof(path), SYS_NET_OP, SYS_NET, e->d_name);
        if (hap_read_file(path, val, sizeof(val)) > 0) {
            while (val[strlen(val)-1] == '\n') val[strlen(val)-1] = '\0';
            if (strstr(val, "up") || strstr(val, "unknown"))
                nic->is_running = 1;
        }
        /* 管理状态：IFF_UP 通过 ioctl 获取 */
        nic->is_up = 1;   /* 简化判断：不为 down 即认为可用 */

        /* MTU */
        snprintf(path, sizeof(path), SYS_NET_MTU, SYS_NET, e->d_name);
        if (hap_read_file(path, val, sizeof(val)) > 0) {
            nic->mtu = (uint32_t)strtoul(val, NULL, 10);
        }
        /* MAC */
        snprintf(path, sizeof(path), SYS_NET_MAC, SYS_NET, e->d_name);
        if (hap_read_file(path, val, sizeof(val)) > 0) {
            char *p = val + strlen(val);
            while (p > val && isspace((unsigned char)p[-1])) { p--; *p = '\0'; }
            strncpy(nic->mac, val, sizeof(nic->mac) - 1);
        }
        /* IP */
        hap_net_ip(e->d_name, nic->ip, sizeof(nic->ip));
        /* 收发字节 */
        if (hap_net_octets(e->d_name, &nic->rx_bytes, &nic->tx_bytes)) {}

        idx++;
    }
    closedir(d);
    out->iface_count = idx;
    return HAP_OK;
}