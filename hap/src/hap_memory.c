/*
 * hap_memory.c — 内存信息获取
 *
 * Linux 下读取 /proc/meminfo：MemTotal, MemFree, MemAvailable, SwapTotal, SwapFree。
 * 已用内存 = Total - Available。降级环境下全部为 0。
 */

#include "../include/hap.h"
#include "hap_util.h"

#define PROC_MEMINFO "/proc/meminfo"

/* 从 /proc/meminfo 读取 "key" 对应的千字节数值，失败返回 0 */
static uint64_t hap_mem_key_kb(const char *meminfo, const char *key) {
    char buf[256];
    if (hap_line_value(meminfo, key, buf, sizeof(buf))) {
        return (uint64_t)strtoull(buf, NULL, 10);
    }
    return 0;
}

int hap_memory_probe(hap_memory_info_t *out) {
    char meminfo[8192];

    if (!out) return HAP_USERES;
    memset(out, 0, sizeof(hap_memory_info_t));

    if (hap_read_file(PROC_MEMINFO, meminfo, sizeof(meminfo)) > 0) {
        out->total_kb = hap_mem_key_kb(meminfo, "MemTotal");
        out->free_kb = hap_mem_key_kb(meminfo, "MemFree");
        /* MemAvailable 是较新内核字段，读取不到时退化为 MemFree */
        out->available_kb = hap_mem_key_kb(meminfo, "MemAvailable");
        if (out->available_kb == 0 && out->total_kb > 0) {
            out->available_kb = out->free_kb;
        }
        out->swap_total_kb = hap_mem_key_kb(meminfo, "SwapTotal");
        out->swap_free_kb = hap_mem_key_kb(meminfo, "SwapFree");
        out->used_kb =
            (out->total_kb > out->available_kb) ? (out->total_kb - out->available_kb) : 0;
    }

    /* 内存类型无法从 /proc 可靠读取；留空表示未知 */
    return HAP_OK;
}