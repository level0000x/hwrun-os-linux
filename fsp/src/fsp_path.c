/*
 * fsp_path.c — FSP 路径操作
 *
 * 提供绝对/相对规范化、join、parent 等纯字符串路径工具，
 * 不依赖目标文件是否真实存在（词法级处理）。
 */

#include "fsp.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* 词法规范化：折叠多个 '/',处理 '.' 与 '..'，返回原串拷贝到 out。
 * 若为相对路径,则基于当前工作目录解析为绝对路径。 */
static int fsp_path_normalize(const char *in, char *out, size_t cap) {
    if (!in || !out || cap == 0) return -EINVAL;

    char absbuf[4096];
    const char *src = in;

    /* 相对/绝对判定 */
    int absolute = (src[0] == '/');

    if (!absolute) {
        /* 相对路径：先拼接 cwd */
        if (!getcwd(absbuf, sizeof(absbuf))) return -errno;
        size_t cwdl = strlen(absbuf);
        size_t src_len = strlen(src);
        if (cwdl + 1 + src_len + 1 > sizeof(absbuf)) return -ENAMETOOLONG;
        absbuf[cwdl++] = '/';
        memcpy(absbuf + cwdl, src, src_len + 1);
        src = absbuf;
        absolute = 1; /* 规范化后统一绝对 */
    }

    /* 组件栈（词法解析，不 stat，避免对不存在路径失败） */
    char stack[64][FSP_NAME_MAX];
    int top = 0;

    char seg[FSP_NAME_MAX];
    const char *p = src;
    size_t i;

    while (*p) {
        while (*p == '/')
            p++;
        if (*p == '\0') break;
        i = 0;
        while (*p && *p != '/') {
            if (i + 1 < sizeof(seg)) seg[i++] = *p;
            p++;
        }
        seg[i] = '\0';

        if (strcmp(seg, ".") == 0) {
            continue;
        } else if (strcmp(seg, "..") == 0) {
            if (top > 0) top--; /* 回退一级（不会越过根） */
        } else {
            if (top < 64) {
                snprintf(stack[top], sizeof(stack[top]), "%s", seg);
                top++;
            } else {
                return -ENAMETOOLONG;
            }
        }
    }

    /* 组装输出 */
    size_t n = 0;
#define PUSH(s)                                                                                    \
    do {                                                                                           \
        for (const char *q = (s); *q; q++) {                                                       \
            if (n + 1 >= cap) return -ENAMETOOLONG;                                                \
            out[n++] = *q;                                                                         \
        }                                                                                          \
    } while (0)

    PUSH("/");
    for (int k = 0; k < top; k++) {
        if (!(n == 1 && out[0] == '/')) PUSH("/");
        PUSH(stack[k]);
    }
#undef PUSH
    out[n] = '\0';
    return 0;
}

/* join: dir + "/" + name，单分隔符拼接并直接规范化 */
static int fsp_path_join(char *out, size_t cap, const char *dir, const char *name) {
    if (!dir || !name || !out || cap == 0) return -EINVAL;

    char tmp[4096];
    size_t dl = strlen(dir);
    int d_needs = (dl > 0 && dir[dl - 1] != '/');
    size_t need = dl + (d_needs ? 1 : 0) + strlen(name);

    if (need + 1 > sizeof(tmp)) return -ENAMETOOLONG;

    size_t n = 0;
    memcpy(tmp, dir, dl);
    n += dl;
    if (d_needs) tmp[n++] = '/';
    strcpy(tmp + n, name);

    return fsp_path_normalize(tmp, out, cap);
}

/* parent: 去掉最后一个路径分量（含文件或目录均适用） */
static int fsp_path_parent(const char *path, char *out, size_t cap) {
    if (!path || !out || cap == 0) return -EINVAL;

    char norm[4096];
    int rc = fsp_path_normalize(path, norm, sizeof(norm));
    if (rc != 0) return rc;

    size_t len = strlen(norm);
    /* 去掉末尾的 '/'（根目录除外） */
    while (len > 1 && norm[len - 1] == '/')
        norm[--len] = '\0';

    /* 去掉最后一个分量 */
    char *slash = strrchr(norm, '/');
    if (!slash) return -EINVAL;

    if (slash == norm) { /* 根目录 */
        if (cap < 2) return -ENAMETOOLONG;
        snprintf(out, cap, "%s", "/");
        return 0;
    }
    *slash = '\0';
    if (*(norm + 1) == '\0') { /* 规范化后仅剩根 */
        snprintf(out, cap, "%s", "/");
        return 0;
    }
    snprintf(out, cap, "%s", norm);
    return 0;
}

/* 绝对化：非绝对路径补 cwd 前缀并规范化（目标无需存在） */
static int fsp_path_absolute(const char *path, char *out, size_t cap) {
    if (!path || !out || cap == 0) return -EINVAL;
    if (path[0] == '/') return fsp_path_normalize(path, out, cap);
    return fsp_path_normalize(path, out, cap); /* normalize 已含相对->绝对逻辑 */
}

void hw_fsp_ops_path_init(hw_fsp_ops_t *ops) {
    ops->path_normalize = fsp_path_normalize;
    ops->path_join = fsp_path_join;
    ops->path_parent = fsp_path_parent;
    ops->path_absolute = fsp_path_absolute;
}
