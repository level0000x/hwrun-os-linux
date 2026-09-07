/*
 * hap_system.c — 系统信息
 *
 * 读取 /etc/hostname、/proc/version（内核版本）、/proc/uptime（运行时长）。
 * os_name 从 /etc/os-release 的 PRETTY_NAME 读取。降级环境下为空/0。
 */

#include "../include/hap.h"
#include "hap_util.h"

#define FILE_HOSTNAME   "/etc/hostname"
#define FILE_UPTIME     "/proc/uptime"
#define FILE_VERSION    "/proc/version"
#define FILE_OSRELEASE  "/etc/os-release"

int hap_system_probe(hap_system_info_t *out) {
    char buf[1024];

    if (!out) return HAP_USERES;
    memset(out, 0, sizeof(hap_system_info_t));

    /* 主机名 */
    if (hap_read_file(FILE_HOSTNAME, out->hostname, sizeof(out->hostname)) > 0) {
        char *p = out->hostname + strlen(out->hostname);
        while (p > out->hostname && isspace((unsigned char)p[-1])) { p--; *p = '\0'; }
    }

    /* 注册主机名 getenv/HOSTNAME 兜底（无 /etc/hostname 时） */
    if (out->hostname[0] == '\0') {
        const char *h = getenv("COMPUTERNAME");
        if (!h) h = getenv("HOSTNAME");
        if (h) strncpy(out->hostname, h, sizeof(out->hostname) - 1);
    }

    /* 运行时长：/proc/uptime 的第一列（秒） */
    if (hap_read_file(FILE_UPTIME, buf, sizeof(buf)) > 0) {
        unsigned long long up;
        if (sscanf(buf, "%llu", &up) == 1) out->uptime_seconds = (uint64_t)up;
    }

    /* /proc/version：如 "Linux version 6.1.31-arch1-1 (gcc...) #1 SMP ..." */
    if (hap_read_file(FILE_VERSION, buf, sizeof(buf)) > 0) {
        char *sp;
        /* 第 3 个词为内核版本号 */
        strncpy(out->kernel_version, buf, sizeof(out->kernel_version) - 1);
        sp = strchr(buf, ' ');
        if (sp) sp = strchr(sp + 1, ' ');
        if (sp) sp = strchr(sp + 1, ' ');
        if (sp) {
            char *end = sp + 1;
            while (*end && !isspace((unsigned char)*end)) end++;
            *end = '\0';
            snprintf(out->kernel_release, sizeof(out->kernel_release), "%s", sp + 1);
        }
    }

    /* 操作系统名称：/etc/os-release 的 PRETTY_NAME */
    if (hap_read_file(FILE_OSRELEASE, buf, sizeof(buf)) > 0) {
        char val[256];
        if (hap_line_value(buf, "PRETTY_NAME", val, sizeof(val))) {
            /* 去掉引号 */
            size_t vlen = strlen(val);
            if (vlen >= 2 && val[0] == '"' && val[vlen-1] == '"') {
                val[vlen-1] = '\0';
                memmove(val, val + 1, vlen);
            }
            strncpy(out->os_name, val, sizeof(out->os_name) - 1);
        }
    }

    return HAP_OK;
}