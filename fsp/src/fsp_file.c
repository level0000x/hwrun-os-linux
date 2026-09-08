/*
 * fsp_file.c — FSP 文件操作
 *
 * 基于 POSIX：open/close/read/write/pread/pwrite/lseek/rename/remove/unlink
 * 以及 stat/lstat/fstat/statfs 状态查询。
 */

#include "fsp.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>

/* 结构体 stat -> fsp_stat */
static void stat_to_fsp(const struct stat *s, fsp_stat_t *o) {
    o->dev = (uint64_t)s->st_dev;
    o->ino = (uint64_t)s->st_ino;
    o->mode = (uint32_t)s->st_mode;
    o->nlink = (uint32_t)s->st_nlink;
    o->uid = (uint32_t)s->st_uid;
    o->gid = (uint32_t)s->st_gid;
    o->size = (uint64_t)s->st_size;
    o->atime = (uint64_t)s->st_atime;
    o->mtime = (uint64_t)s->st_mtime;
    o->ctime = (uint64_t)s->st_ctime;
    o->blocks = (uint64_t)s->st_blocks;
    o->blksize = (uint32_t)s->st_blksize;
}

/* ---------- 打开/关闭 ---------- */
static int fsp_open(const char *path, int flags, mode_t mode) {
    return open(path, flags, mode);
}

static int fsp_openat(const char *dirpath, const char *path, int flags, mode_t mode) {
    /* 相对路径基于 dirpath 解析；绝对路径或空 dirpath 直接按原样打开 */
    if (!path || path[0] == '\0') return -EINVAL;
    if (!dirpath || dirpath[0] == '\0' || path[0] == '/') return open(path, flags, mode);

    char full[4096];
    if (snprintf(full, sizeof(full), "%s/%s", dirpath, path) >= (int)sizeof(full))
        return -ENAMETOOLONG;
    return open(full, flags, mode);
}

static int fsp_close(int fd) {
    if (fd < 0) return -EBADF;
    return close(fd);
}

/* ---------- 读/写 ---------- */
static ssize_t fsp_read(int fd, void *buf, size_t count) {
    return read(fd, buf, count);
}

static ssize_t fsp_write(int fd, const void *buf, size_t count) {
    return write(fd, buf, count);
}

static ssize_t fsp_pread(int fd, void *buf, size_t count, off_t off) {
    return pread(fd, buf, count, off);
}

static ssize_t fsp_pwrite(int fd, const void *buf, size_t count, off_t off) {
    return pwrite(fd, buf, count, off);
}

/* ---------- 定位/改名/删除 ---------- */
static off_t fsp_seek(int fd, off_t off, int whence) {
    return lseek(fd, off, whence);
}

static int fsp_rename(const char *oldpath, const char *newpath) {
    return rename(oldpath, newpath);
}

static int fsp_remove(const char *path) {
    return remove(path);
}

static int fsp_unlink(const char *path) {
    return unlink(path);
}

/* ---------- 状态 ---------- */
static int fsp_stat(const char *path, fsp_stat_t *st) {
    struct stat s;
    if (stat(path, &s) != 0) return -errno;
    stat_to_fsp(&s, st);
    return 0;
}

static int fsp_lstat(const char *path, fsp_stat_t *st) {
    struct stat s;
    if (lstat(path, &s) != 0) return -errno;
    stat_to_fsp(&s, st);
    return 0;
}

static int fsp_fstat(int fd, fsp_stat_t *st) {
    struct stat s;
    if (fstat(fd, &s) != 0) return -errno;
    stat_to_fsp(&s, st);
    return 0;
}

static int fsp_access(const char *path, int amode) {
    return access(path, amode);
}

static int fsp_statfs(const char *path, fsp_statfs_t *fs) {
    struct statfs s;
    if (statfs(path, &s) != 0) return -errno;
    fs->f_type = (uint64_t)s.f_type;
    fs->f_bsize = (uint64_t)s.f_bsize;
    fs->f_blocks = (uint64_t)s.f_blocks;
    fs->f_bfree = (uint64_t)s.f_bfree;
    fs->f_bavail = (uint64_t)s.f_bavail;
    fs->f_files = (uint64_t)s.f_files;
    fs->f_ffree = (uint64_t)s.f_ffree;
    fs->f_namelen = (uint64_t)s.f_namelen;
    return 0;
}

/* 组装文件操作组 */
void hw_fsp_ops_file_init(hw_fsp_ops_t *ops) {
    ops->open = fsp_open;
    ops->openat = fsp_openat;
    ops->close = fsp_close;
    ops->read = fsp_read;
    ops->write = fsp_write;
    ops->pread = fsp_pread;
    ops->pwrite = fsp_pwrite;
    ops->seek = fsp_seek;
    ops->rename = fsp_rename;
    ops->remove = fsp_remove;
    ops->unlink = fsp_unlink;

    ops->stat = fsp_stat;
    ops->lstat = fsp_lstat;
    ops->fstat = fsp_fstat;
    ops->access = fsp_access;
    ops->statfs = fsp_statfs;
}