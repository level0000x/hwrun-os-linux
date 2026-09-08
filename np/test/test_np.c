/*
 * test_np.c — NP 插件 dlopen 自测
 *
 * 编译后加载 build/np.so，验证插件出口、生命周期、协议接口，
 * 并真实执行：接口枚举、名称解析、UDP 环回收发、socket 生命周期。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "hwrun.h"
#include "np.h"

static int failures = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond)                                                                                  \
            printf("  [ok] %s\n", msg);                                                            \
        else {                                                                                     \
            printf("  [FAIL] %s\n", msg);                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void) {
    void *h = dlopen("build/np.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        printf("dlopen failed: %s\n", dlerror());
        return 1;
    }
    printf("dlopen np.so ok\n");

    hw_plugin_t *(*entry)(void) = (hw_plugin_t * (*)(void)) dlsym(h, "hw_plugin_entry");
    CHECK(entry != NULL, "导出 hw_plugin_entry()");

    hw_plugin_t *p = entry();
    CHECK(p != NULL, "hw_plugin_entry() 非空");
    printf("  plugin id=%s ver=%s type=%d provides=%s requires=%s/%s/%s/%s\n", p->id, p->version,
           (int)p->type, p->provides ? p->provides[0] : "?",
           p->requires && p->requires_count > 0 ? p->requires[0] : "?",
           p->requires && p->requires_count > 1 ? p->requires[1] : "?",
           p->requires && p->requires_count > 2 ? p->requires[2] : "?",
           p->requires && p->requires_count > 3 ? p->requires[3] : "?");
    CHECK(strcmp(p->id, "np") == 0, "id == np");
    CHECK(p->type == HWPLUGIN_TYPE_NETWORK, "type == network");
    CHECK(p->provides && p->provides_count == 1 && strcmp(p->provides[0], "NP") == 0,
          "provides == NP");

    CHECK(p->ops.init && p->ops.init(p) == 0, "init() == 0");
    CHECK(p->ops.start && p->ops.start(p) == 0, "start() == 0");

    hw_np_ops_t *n = (hw_np_ops_t *)p->ops.get_interface("NP");
    CHECK(n != NULL, "get_interface(\"NP\") 非空");
    CHECK(p->ops.get_interface("XXX") == NULL, "get_interface(\"XXX\") == NULL");
    CHECK(n->socket && n->bind && n->listen && n->connect && n->accept && n->send && n->recv &&
              n->close && n->shutdown,
          "socket 基础接口齐备");
    CHECK(n->sendto && n->recvfrom, "sendto/recvfrom 齐备");
    CHECK(n->resolve && n->host_to_ip && n->gethostname, "名称解析接口齐备");
    CHECK(n->get_interfaces && n->conn_list && n->conn_get && n->get_stats,
          "接口/连接/统计接口齐备");

    /* 名称解析 */
    {
        char ip[64] = {0};
        int rc = n->host_to_ip("localhost", ip, sizeof(ip));
        printf("  localhost -> %s (rc=%d)\n", ip[0] ? ip : "(空)", rc);
        CHECK(rc == 0 && ip[0], "host_to_ip(localhost) 成功");
    }

    /* UDP 环回收发 */
    {
        int fd = -1;
        if (n->socket(NP_AF_INET, NP_SOCK_DGRAM, 0, &fd) == 0 && fd >= 0) {
            if (n->bind(fd, "127.0.0.1", 0) == 0) {
                int sfd = -1;
                if (n->socket(NP_AF_INET, NP_SOCK_DGRAM, 0, &sfd) == 0) {
                    struct sockaddr_in peer;
                    memset(&peer, 0, sizeof(peer));
                    peer.sin_family = AF_INET;
                    peer.sin_port = 0; /* 由 connect 目标端口组合：需接收端端口 */
                    /* 用 getsockname 取绑定端口 */
                    struct sockaddr_storage ss;
                    socklen_t sl = sizeof(ss);
                    getsockname(fd, (struct sockaddr *)&ss, &sl);
                    unsigned port = ntohs(((struct sockaddr_in *)&ss)->sin_port);
                    peer.sin_port = htons((uint16_t)port);
                    inet_pton(AF_INET, "127.0.0.1", &peer.sin_addr);

                    const char *msg = "HWRun-NP-echo";
                    ssize_t sn =
                        n->sendto(sfd, msg, strlen(msg), 0, (struct sockaddr *)&peer, sizeof(peer));
                    if (sn >= 0) {
                        char buf[128];
                        ssize_t rn = n->recvfrom(fd, buf, sizeof(buf), 0, NULL, NULL);
                        printf("  UDP 发送 %zd 字节，接收 %zd 字节\n", sn, rn);
                        CHECK(rn == (ssize_t)strlen(msg) && strncmp(buf, msg, (size_t)rn) == 0,
                              "UDP 环回 echo 一致");
                    } else {
                        printf("  sendto rc=%zd\n", sn);
                        CHECK(0, "UDP sendto 成功");
                    }
                    n->close(sfd);
                }
            }
            n->close(fd);
        } else {
            CHECK(0, "UDP socket 创建");
        }
    }

    /* 接口枚举 */
    {
        np_interface_t ifs[64];
        int c = 0;
        int rc = n->get_interfaces(ifs, 64, &c);
        printf("  get_interfaces rc=%d count=%d\n", rc, c);
        CHECK(rc == 0, "get_interfaces 成功");
        for (int i = 0; i < c && i < 3; i++) {
            printf("    iface[%d] %-8s up=%d mtu=%u mac=%s ip=%s gw=%s rx=%llu tx=%llu\n", i,
                   ifs[i].name, ifs[i].up, ifs[i].mtu, ifs[i].mac, ifs[i].ipv4, ifs[i].gateway,
                   (unsigned long long)ifs[i].rx_bytes, (unsigned long long)ifs[i].tx_bytes);
        }
    }

    /* 连接表 & 统计 */
    {
        int fd = -1;
        n->socket(NP_AF_INET, NP_SOCK_STREAM, 0, &fd);
        if (fd >= 0) {
            np_socket_t s;
            CHECK(n->conn_get(fd, &s) == 0 && s.fd == fd, "conn_get 命中新建 socket");
            n->close(fd);
            CHECK(n->conn_get(fd, &s) != 0, "close 后 conn_get 失效");
        }
        np_stats_t st;
        if (n->get_stats(&st) == 0) {
            printf("    stats: iface_count=%llu tcp=%llu udp=%llu unix=%llu total=%llu rx=%llu "
                   "tx=%llu\n",
                   (unsigned long long)st.iface_count, (unsigned long long)st.tcp_sockets,
                   (unsigned long long)st.udp_sockets, (unsigned long long)st.unix_sockets,
                   (unsigned long long)st.total_sockets, (unsigned long long)st.rx_bytes,
                   (unsigned long long)st.tx_bytes);
        }
    }

    /* Unix 域 socket 创建/关闭 */
    {
        int fd = -1;
        CHECK(n->socket(NP_AF_UNIX, NP_SOCK_STREAM, 0, &fd) == 0 && fd >= 0, "unix socket 创建");
        if (fd >= 0) n->close(fd);
    }

    CHECK(p->ops.stop && p->ops.stop(p) == 0, "stop() == 0");
    CHECK(p->ops.destroy && p->ops.destroy(p) == 0, "destroy() == 0");

    dlclose(h);
    printf("== %s (%d failures) ==\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}