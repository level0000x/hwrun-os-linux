/*
 * fsp_dir.c — FSP 目录操作
 *
 * mkdir/rmdir/exists/list，基于 POSIX opendir/readdir。
 */

#include "fsp.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

static int fsp_mkdir(const char *path, mode_t mode) {
    return mkdir(path, mode);
}

static int fsp_rmdir(const char *path) {
    return rmdir(path);
}

/* 存在性检查：返回 1=存在 0=不存在，负 errno=错误 */
static int fsp_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return 1;
    if (errno == ENOENT || errno == ENOTDIR) return 0;
    return -errno;
}

/* 列出目录项，跳过 . 和 ..，返回实际条目数；失败返回负 errno */
static int fsp_list(const char *path, fsp_dirent_t *entries, int max) {
    DIR *d = opendir(path);
    if (!d) return -errno;

    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        const char *nm = e->d_name;
        if (nm[0] == '.') {
            if (nm[1] == '\0') continue;             /* .  */
            if (nm[1] == '.' && nm[2] == '\0') continue; /* .. */
        }
        if (n >= max) break;                          /* 缓冲区满，停止 */

        if (entries) {
            fsp_dirent_t *en = &entries[n];
            en->ino  = (uint64_t)e->d_ino;
            en->type = (uint32_t)e->d_type;
            snprintf(en->name, sizeof(en->name), "%s", nm);
        }
        n++;
    }
    closedir(d);
    return n;
}

void hw_fsp_ops_dir_init(hw_fsp_ops_t *ops) {
    ops->mkdir  = fsp_mkdir;
    ops->rmdir  = fsp_rmdir;
    ops->exists = fsp_exists;
    ops->list   = fsp_list;
}