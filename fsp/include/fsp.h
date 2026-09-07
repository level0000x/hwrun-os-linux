/*
 * fsp.h — FSP（FileSystem Protocol）文件系统协议插件公共头文件
 *
 * 通过 hw_plugin_ops_t::get_interface("FSP") 返回 hw_fsp_ops_t*，
 * 上层插件（LOADER / SHELL / PKGMGR / CONFIG / LOG / GIT 等）据此访问
 * 真实可用的 POSIX 文件系统操作。
 *
 * 全部接口为 POSIX 语义，在 MSYS2 / Linux 等原生环境直接可用。
 */

#ifndef HWRUN_FSP_H
#define HWRUN_FSP_H

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 文件状态（对应 POSIX struct stat，字段与设计文档一致）
 * ============================================================ */
typedef struct fsp_stat {
    uint64_t dev;               /* 设备 ID          */
    uint64_t ino;               /* inode 号         */
    uint32_t mode;              /* 文件类型与权限    */
    uint32_t nlink;             /* 硬链接数          */
    uint32_t uid;               /* 拥有者 UID        */
    uint32_t gid;               /* 拥有组 GID        */
    uint64_t size;              /* 文件字节数        */
    uint64_t atime;             /* 访问时间 (秒)     */
    uint64_t mtime;             /* 修改时间 (秒)     */
    uint64_t ctime;             /* 状态变更时间 (秒) */
    uint64_t blocks;            /* 512 字节块数      */
    uint32_t blksize;           /* 块大小            */
} fsp_stat_t;

/* ============================================================
 * 目录项（list/readdir 用），与设计文档一致
 * ============================================================ */
#define FSP_NAME_MAX 256

typedef struct fsp_dirent {
    uint64_t ino;               /* inode 号   */
    uint32_t type;              /* DT_REG/DT_DIR/DT_LNK ... */
    char     name[FSP_NAME_MAX];/* 文件名     */
} fsp_dirent_t;

/* ============================================================
 * 文件系统统计（对应 POSIX struct statfs）
 * ============================================================ */
typedef struct fsp_statfs {
    uint64_t f_type;            /* 文件系统类型  */
    uint64_t f_bsize;           /* 块大小        */
    uint64_t f_blocks;          /* 总块数        */
    uint64_t f_bfree;           /* 空闲块数      */
    uint64_t f_bavail;          /* 可用块数      */
    uint64_t f_files;           /* 总 inode 数   */
    uint64_t f_ffree;           /* 空闲 inode 数 */
    uint64_t f_namelen;         /* 最大文件名长度 */
} fsp_statfs_t;

/* ============================================================
 * 挂载点信息（/proc/mounts 一行）
 * ============================================================ */
typedef struct fsp_mountinfo {
    char device[256];           /* 源设备 / 远程路径 */
    char mountpoint[256];       /* 挂载点           */
    char fstype[64];            /* 文件系统类型     */
    char options[256];          /* 挂载选项        */
    uint32_t freq;              /* 转储频率         */
    uint32_t passno;            /* fsck 顺序        */
} fsp_mountinfo_t;

/* ============================================================
 * FSP 协议接口（get_interface("FSP") 返回）
 *
 * 返回值约定：成功为 0（或实际读写的字节数 / fd），失败返回 -1 并置
 * errno；与 POSIX 语义保持一致，便于上层插件直接使用。
 * ============================================================ */
typedef struct hw_fsp_ops {
    /* ---------- 文件操作 ---------- */
    int    (*open)(const char *path, int flags, mode_t mode);      /* 成功为 fd，失败 -1 */
    int    (*openat)(const char *dirpath, const char *path,
                     int flags, mode_t mode);
    int    (*close)(int fd);
    ssize_t(*read)(int fd, void *buf, size_t count);               /* 当前偏移读 */
    ssize_t(*write)(int fd, const void *buf, size_t count);        /* 当前偏移写 */
    ssize_t(*pread)(int fd, void *buf, size_t count, off_t off);
    ssize_t(*pwrite)(int fd, const void *buf, size_t count, off_t off);
    off_t  (*seek)(int fd, off_t off, int whence);                 /* 新偏移，失败 -1 */
    int    (*rename)(const char *oldpath, const char *newpath);
    int    (*remove)(const char *path);                            /* 删除文件/空目录 */
    int    (*unlink)(const char *path);                            /* 仅删除文件 */

    /* ---------- 文件状态 ---------- */
    int    (*stat)(const char *path, fsp_stat_t *st);
    int    (*lstat)(const char *path, fsp_stat_t *st);
    int    (*fstat)(int fd, fsp_stat_t *st);
    int    (*access)(const char *path, int amode);                 /* R_OK/W_OK/X_OK/F_OK */
    int    (*statfs)(const char *path, fsp_statfs_t *fs);

    /* ---------- 目录操作 ---------- */
    int    (*mkdir)(const char *path, mode_t mode);
    int    (*rmdir)(const char *path);
    int    (*exists)(const char *path);                            /* 1=存在 0=不存在 -1=错误 */
    int    (*list)(const char *path, fsp_dirent_t *entries, int max);/* 返回个数，跳过 . .. */

    /* ---------- 链接操作 ---------- */
    int    (*symlink)(const char *target, const char *linkpath);
    ssize_t(*readlink)(const char *path, char *buf, size_t size);

    /* ---------- 权限操作 ---------- */
    int    (*chmod)(const char *path, mode_t mode);
    int    (*chown)(const char *path, uid_t uid, gid_t gid);

    /* ---------- 路径操作 ---------- */
    int    (*path_normalize)(const char *in, char *out, size_t cap);
    int    (*path_join)(char *out, size_t cap,
                        const char *dir, const char *name);
    int    (*path_parent)(const char *path, char *out, size_t cap);
    int    (*path_absolute)(const char *path, char *out, size_t cap);

    /* ---------- 挂载点信息 ---------- */
    int    (*mount_list)(fsp_mountinfo_t *infos, int max);         /* 返回条数，失败 -1 */
} hw_fsp_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_FSP_H */