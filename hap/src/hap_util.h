/*
 * hap_util.h — HAP 内部工具函数（读取 /proc 与 /sys，带优雅降级）
 *
 * 统一封装文件读取，保证任何路径不存在/无权限时返回 0 与空，不崩溃。
 */

#ifndef HW_HAP_UTIL_H
#define HW_HAP_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* 一次性读取一个文本文件进入静态缓冲。返回实际读入字节数，失败返回 0。 */
static inline size_t hap_read_file(const char *path, char *buf, size_t cap) {
    FILE *f;
    size_t n = 0;
    if (!path || !buf || cap == 0) return 0;
    buf[0] = '\0';
    f = fopen(path, "r");
    if (!f) return 0;
    n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    fclose(f);
    return n;
}

/* 在某配置文件中查找 "key" 并将其对应值（跳过空白到行尾）填入 out。
 * 返回 1 找到，0 未找到。 */
static inline int hap_line_value(const char *cfg, const char *key,
                                 char *out, size_t cap) {
    const char *p = cfg;
    size_t klen = strlen(key);
    char *val;
    if (!cfg || !key || !out || cap == 0) return 0;
    out[0] = '\0';
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
        /* 匹配行首 key */
        if (linelen > klen && strncmp(p, key, klen) == 0 &&
            (p[klen] == ':' || p[klen] == '=' || isspace((unsigned char)p[klen]))) {
            const char *vp = p + klen;
            while (*vp == ':' || *vp == '=' || isspace((unsigned char)*vp)) vp++;
            val = out;
            while (*vp && *vp != '\n' && (size_t)(val - out) < cap - 1) {
                *val++ = *vp++;
            }
            *val = '\0';
            /* 去掉尾部空白 */
            while (val > out && isspace((unsigned char)val[-1])) { val--; *val = '\0'; }
            return 1;
        }
        if (!nl) break;
        p = nl + 1;
    }
    return 0;
}

#endif /* HW_HAP_UTIL_H */