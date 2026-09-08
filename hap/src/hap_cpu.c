/*
 * hap_cpu.c — CPU 信息获取
 *
 * Linux 下通过 /proc/cpuinfo、/proc/loadavg 与 /sys/devices/system/cpu
 * 真实读取核心数、型号与频率；负载取自 /proc/loadavg。
 * 在 MSYS2/Windows 降级环境下读取不到则返回 0 / 空串，不崩溃。
 */

#include "../include/hap.h"
#include "hap_util.h"

#define PROC_CPUINFO "/proc/cpuinfo"
#define PROC_LOADAVG "/proc/loadavg"
#define SYS_CPU_CUR "%s/devices/system/cpu/cpu%u/cpufreq/scaling_cur_freq"
#define SYS_CPU_MAX "%s/devices/system/cpu/cpu%u/cpufreq/scaling_max_freq"
#define SYS_CPU_MIN "%s/devices/system/cpu/cpu%u/cpufreq/scaling_min_freq"

/* 解析负载均值：/proc/loadavg 形如 "0.52 0.58 0.59 1/4 2061" */
static void hap_cpu_loadavg(hap_cpu_info_t *info) {
    char buf[128];
    char a[16], b[16], c[16];
    if (hap_read_file(PROC_LOADAVG, buf, sizeof(buf)) > 0) {
        if (sscanf(buf, "%15s %15s %15s", a, b, c) == 3) {
            info->load1m = atof(a);
            info->load5m = atof(b);
            info->load15m = atof(c);
        }
    }
}

int hap_cpu_probe(hap_cpu_info_t *out) {
    char cfg[4096];
    char val[256];
    char sys_path[256];
    const char *sysroot = "/sys";

    if (!out) return HAP_USERES;
    memset(out, 0, sizeof(hap_cpu_info_t));

    /* 1) 型号与厂商：/proc/cpuinfo */
    if (hap_read_file(PROC_CPUINFO, cfg, sizeof(cfg)) > 0) {
        if (hap_line_value(cfg, "model name", out->model, sizeof(out->model))) {
            /* 记录到 ident */
        } else if (hap_line_value(cfg, "Hardware", out->model, sizeof(out->model))) {
            /* ARM 类 */
        }
        if (!hap_line_value(cfg, "vendor_id", val, sizeof(val))) {
            /* ARM 没有 vendor_id */
            val[0] = '\0';
        }
        strncpy(out->vendor, val, sizeof(out->vendor) - 1);
        if (out->model[0] == '\0') {
            /* 退而求其次使用 Hardware */
            if (hap_line_value(cfg, "Hardware", out->model, sizeof(out->model))) {
            }
        }

        /* 2) 逻辑核心数：统计 "processor" 关键字出现次数 */
        {
            const char *p = cfg;
            unsigned count = 0;
            while ((p = strstr(p, "\nprocessor")) != NULL) {
                count++;
                p += 10;
            }
            if (count == 0) count = 1; /* 至少 1 核 */
            out->cores = count;
        }
    } else {
        /* 无 /proc/cpuinfo：无法取得核心数与型号 */
        out->cores = 0;
    }
    if (out->sockets == 0) out->sockets = 1;

    /* 3) 当前/最小/最大频率：优先 /sys cpufreq（MHz） */
    if (out->cores > 0) {
        snprintf(sys_path, sizeof(sys_path), SYS_CPU_CUR, sysroot, 0u);
        if (hap_read_file(sys_path, val, sizeof(val)) > 0) {
            out->freq_current_mhz = (uint64_t)strtoull(val, NULL, 10) / 1000;
        }
        snprintf(sys_path, sizeof(sys_path), SYS_CPU_MAX, sysroot, 0u);
        if (hap_read_file(sys_path, val, sizeof(val)) > 0) {
            out->freq_max_mhz = (uint64_t)strtoull(val, NULL, 10) / 1000;
        }
        snprintf(sys_path, sizeof(sys_path), SYS_CPU_MIN, sysroot, 0u);
        if (hap_read_file(sys_path, val, sizeof(val)) > 0) {
            out->freq_min_mhz = (uint64_t)strtoull(val, NULL, 10) / 1000;
        }
    }

    /* 4) 负载均值 */
    hap_cpu_loadavg(out);

    /* 5) 组装 ident：例如 "8 CPU / Intel Core i7-..." */
    if (out->model[0]) {
        snprintf(out->ident, sizeof(out->ident), "%u CPU / %s", out->cores, out->model);
    } else {
        snprintf(out->ident, sizeof(out->ident), "%u CPU", out->cores);
    }

    return HAP_OK;
}