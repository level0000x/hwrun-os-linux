/*
 * fsp_perm.c / fsp_link.c — FSP 权限与链接操作
 *
 * chmod/chown、symlink/readlink，基于 POSIX 直连内核语义。
 */

#include "fsp.h"

#include <errno.h>
#include <unistd.h>

/* ---------- 权限 ---------- */
static int fsp_chmod(const char *path, mode_t mode) {
    return chmod(path, mode);
}

static int fsp_chown(const char *path, uid_t uid, gid_t gid) {
    return chown(path, uid, gid);
}

/* ---------- 符号链接 ---------- */
static int fsp_symlink(const char *target, const char *linkpath) {
    return symlink(target, linkpath);
}

static ssize_t fsp_readlink(const char *path, char *buf, size_t size) {
    ssize_t n = readlink(path, buf, size > 0 ? size - 1 : 0);
    if (n >= 0 && size > 0) buf[n] = '\0'; /* 保证 NUL 结尾，便于上层使用 */
    return n;
}

void hw_fsp_ops_perm_init(hw_fsp_ops_t *ops) {
    ops->chmod = fsp_chmod;
    ops->chown = fsp_chown;
    ops->symlink = fsp_symlink;
    ops->readlink = fsp_readlink;
}