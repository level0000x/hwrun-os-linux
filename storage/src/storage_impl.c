/*
 * storage_impl.c — STORAGE 协议实现：用户态目录卷管理（真实文件系统操作）
 *
 * 范畴（对齐 storage.h 头注与设计稿《HWRun OS STORAG.txt》用户态部分）：
 *   - 卷注册表：目录即卷。create_volume 确保后端目录存在；delete_volume 对
 *     路径做存在/类型校验后递归删除；unregister_volume 仅摘元数据。
 *   - 统计：volume_df = 递归 du（卷真实占用/文件数）+ statvfs（所在文件系统
 *     容量/已用/可用），全部真实取值。
 *   - 快照：目录级逐字节副本（非块级/写时复制，宿主边界见头注），快照目录归
 *     本插件自管（<state_dir>/snapshots/<卷名>/<快照名>），快照恢复 = 清空
 *     目标卷后把快照内容复制回去。
 *   - 持久化：卷/快照注册表全量写入 $HWRUN_STATE/storage.state（可读文本），
 *     tmp+rename 原子落盘；init（storage_impl_init）时整表重载。
 *
 * 边界（不在此实现，注释声明）：Device Mapper / LVM / RAID / 精简池 / 配额
 * 强制 / mkfs / mount 由宿主 Linux 承担；本实现操作的是真实目录路径。
 *
 * 线程模型：注册表由 pthread 互斥锁保护；公开 ops 各自持锁一次，内部辅助
 * 函数不再加锁（避免递归死锁）。错误约定：成功 0，失败负 errno。
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 供 storage_core.c 引用的生命周期入口（本文件实现） */
int storage_impl_init(void);
void storage_impl_shutdown(void);

/* 供 ops 表引用的内部实现（前置声明，避免 -Wmissing-prototypes） */
static int storage_create_volume(const hw_storage_volume_def_t *def, hw_storage_volume_info_t *out);
static int storage_delete_volume(const char *name_or_id);
static int storage_unregister_volume(const char *name_or_id);
static int storage_list_volumes(hw_storage_volume_info_t *out, int cap, int *count);
static int storage_get_volume(const char *name_or_id, hw_storage_volume_info_t *out);
static int storage_volume_df(const char *name_or_id, hw_storage_df_t *out);
static int storage_snapshot_create(const char *name_or_id, const char *snapshot_name,
                                   hw_storage_snapshot_info_t *out);
static int storage_snapshot_restore(const char *snapshot_name, const char *target_volume);
static int storage_snapshot_delete(const char *snapshot_name);
static int storage_list_snapshots(const char *volume, hw_storage_snapshot_info_t *out, int cap,
                                  int *count);

/* ============================================================
 * 内部数据结构与上下文
 * ============================================================ */
#define STORAGE_STATE_FILE "storage.state"
#define STORAGE_SNAP_ROOT "snapshots"
#define STORAGE_PATH_MAX 512
#define STORAGE_DIR_MAX 240 /* 状态目录上限：须容 快照子路径 505 字内(<512) */
#define STORAGE_ID_HEX 32   /* id[40] 容纳 32 hex + NUL */

typedef struct hw_storage_volume_rec {
    char id[40];
    char name[128];
    char path[STORAGE_PATH_MAX];
    char description[256];
    uint64_t size_limit;
    uint64_t created_at;
    int readonly;
    struct hw_storage_volume_rec *next;
} storage_volume_rec_t;

typedef struct hw_storage_snapshot_rec {
    char id[40];
    char name[128];
    char volume[128]; /* 源卷名称 */
    char path[STORAGE_PATH_MAX];
    uint64_t created_at;
    int active;
    struct hw_storage_snapshot_rec *next;
} storage_snapshot_rec_t;

typedef struct hw_storage_ctx {
    int initialized;
    char state_dir[STORAGE_DIR_MAX];
    char state_file[STORAGE_PATH_MAX];
    storage_volume_rec_t *volumes;
    storage_snapshot_rec_t *snapshots;
    pthread_mutex_t lock;
} storage_ctx_t;

static storage_ctx_t g_ctx;

/* ============================================================
 * 文本工具
 * ============================================================ */
static char *storage_strtrim(char *s) {
    if (!s) return NULL;
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
    return s;
}

/* ============================================================
 * ID 生成：urandom 16 字节 hex（不可用则退化为时间/进程号混洗，仍唯一可用）
 * ============================================================ */
static void storage_gen_id(char *out, size_t cap) {
    unsigned char b[16];
    int filled = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        while (filled < (int)sizeof(b)) {
            ssize_t r = read(fd, b + filled, sizeof(b) - (size_t)filled);
            if (r <= 0) break;
            filled += (int)r;
        }
        close(fd);
    }
    if (filled < (int)sizeof(b)) {
        /* 退化路径：确定性混洗填充剩余字节 */
        uint64_t x = ((uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32)) + (uint64_t)filled;
        for (int i = filled; i < (int)sizeof(b); i++) {
            x = x * 6364136223846793005ULL + 1442695040888963407ULL;
            b[i] = (unsigned char)(x >> 40);
        }
    }
    for (int i = 0; i < (int)sizeof(b) && (size_t)(i * 2 + 2) < cap; i++)
        snprintf(out + i * 2, cap - (size_t)(i * 2), "%02x", b[i]);
}

/* ============================================================
 * 目录工具：mkdir -p / 递归删除(nftw) / 递归复制 / 清空目录
 * ============================================================ */
static int storage_mkdir_p(const char *path, mode_t mode) {
    char tmp[STORAGE_PATH_MAX];
    if (strlen(path) >= sizeof(tmp)) return -ENAMETOOLONG;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                int e = errno;
                *p = '/';
                return -e;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -errno;
    struct stat st;
    if (lstat(path, &st) != 0) return -errno;
    if (!S_ISDIR(st.st_mode)) return -ENOTDIR;
    return 0;
}

static int storage_rm_cb(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
    (void)sb;
    (void)ftwbuf;
    int rc = (tflag == FTW_DP) ? rmdir(fpath) : remove(fpath);
    if (rc != 0) return -errno;
    return 0;
}

/* 递归删除目录树：FTW_DEPTH|FTW_PHYS —— 目录后序访问、绝不跟随符号链接。
 * nftw 返回 0=成功、回调返回的 -errno=删除失败、-1=nftw 自身错误。 */
static int storage_rm_rf(const char *path) {
    return nftw(path, storage_rm_cb, 64, FTW_DEPTH | FTW_PHYS);
}

/* 清空目录内容（保留目录本身），供快照恢复前使用 */
static int storage_dir_clear(const char *path) {
    DIR *d = opendir(path);
    if (!d) return -errno;
    struct dirent *e;
    int rc = 0;
    while (!rc && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char child[1024]; /* 目录路径 + '/' + 目录项，留足余量 */
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        struct stat st;
        if (lstat(child, &st) != 0) {
            rc = -errno;
            break;
        }
        if (S_ISDIR(st.st_mode))
            rc = storage_rm_rf(child);
        else if (remove(child) != 0)
            rc = -errno;
    }
    closedir(d);
    return rc;
}

/* 复制单个常规文件（逐字节流式），并保留源权限位 */
static int storage_copy_file(const char *src, const char *dst) {
    struct stat st;
    if (lstat(src, &st) != 0) return -errno;
    FILE *fi = fopen(src, "rb");
    if (!fi) return -errno;
    FILE *fo = fopen(dst, "wb");
    if (!fo) {
        int e = -errno;
        fclose(fi);
        return e;
    }
    char buf[65536];
    int rc = 0;
    for (;;) {
        size_t r = fread(buf, 1, sizeof(buf), fi);
        if (r > 0 && fwrite(buf, 1, r, fo) != r) {
            rc = -EIO;
            break;
        }
        if (r < sizeof(buf)) {
            if (ferror(fi)) rc = -EIO;
            break;
        }
    }
    fclose(fi);
    if (fclose(fo) != 0 && rc == 0) rc = -EIO;
    if (rc == 0)
        chmod(dst, st.st_mode & 07777);
    else
        remove(dst);
    return rc;
}

/* 递归复制目录树：目录/常规文件/符号链接；特殊文件(fifo/设备)跳过不中断。
 * dst 目录以 0700 创建，收尾 chmod 回源权限，避免只读源目录导致复制中途失败。 */
static int storage_copy_tree(const char *src, const char *dst) {
    struct stat st;
    if (lstat(src, &st) != 0) return -errno;
    if (S_ISDIR(st.st_mode)) {
        int rc = storage_mkdir_p(dst, 0700);
        if (rc != 0) return rc;
        DIR *d = opendir(src);
        if (!d) return -errno;
        struct dirent *e;
        while (!rc && (e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char sp[1024];
            char dp[1024];
            snprintf(sp, sizeof(sp), "%s/%s", src, e->d_name);
            snprintf(dp, sizeof(dp), "%s/%s", dst, e->d_name);
            rc = storage_copy_tree(sp, dp);
        }
        closedir(d);
        if (rc == 0) chmod(dst, st.st_mode & 07777);
        return rc;
    }
    if (S_ISREG(st.st_mode)) return storage_copy_file(src, dst);
    if (S_ISLNK(st.st_mode)) {
        char target[1024];
        ssize_t n = readlink(src, target, sizeof(target) - 1);
        if (n < 0) return -errno;
        target[n] = '\0';
        if (symlink(target, dst) != 0) return -errno;
        return 0;
    }
    return 0; /* 特殊文件：宿主边界，跳过 */
}

/* ============================================================
 * 输入校验
 * ============================================================ */
/* 标签（卷名/快照名）：不得为空、不得含 '/' 与控制字符、不得为 "." ".." */
static int storage_valid_label(const char *s, size_t max_len) {
    if (!s || !s[0]) return -EINVAL;
    if (strlen(s) >= max_len) return -ENAMETOOLONG;
    for (const char *p = s; *p; p++) {
        if (*p == '/' || *p == '\n' || *p == '\r' || (unsigned char)*p < 0x20) return -EINVAL;
    }
    if (!strcmp(s, ".") || !strcmp(s, "..")) return -EINVAL;
    return 0;
}

/* 后端路径：绝对路径、长度受限、无换行。递归删除前另有存在/类型校验。 */
static int storage_valid_path(const char *s) {
    if (!s || !s[0]) return -EINVAL;
    if (strlen(s) >= STORAGE_PATH_MAX) return -ENAMETOOLONG;
    if (s[0] != '/') return -EINVAL;
    if (strchr(s, '\n') || strchr(s, '\r')) return -EINVAL;
    return 0;
}

/* 清理描述中的换行/回车，保证单行可读文本可往返 */
static void storage_sanitize_desc(char *out, size_t cap, const char *in) {
    out[0] = '\0';
    if (!in) return;
    size_t i = 0;
    for (const char *p = in; *p && i + 1 < cap; p++) {
        if (*p == '\n' || *p == '\r') continue;
        out[i++] = *p;
    }
    out[i] = '\0';
}

/* ============================================================
 * 注册表查找
 * ============================================================ */
static storage_volume_rec_t *storage_find_volume(const char *name_or_id) {
    for (storage_volume_rec_t *v = g_ctx.volumes; v; v = v->next) {
        if (!strcmp(v->id, name_or_id) || !strcmp(v->name, name_or_id)) return v;
    }
    return NULL;
}

static storage_snapshot_rec_t *storage_find_snapshot(const char *name_or_id) {
    for (storage_snapshot_rec_t *s = g_ctx.snapshots; s; s = s->next) {
        if (!strcmp(s->id, name_or_id) || !strcmp(s->name, name_or_id)) return s;
    }
    return NULL;
}

/* 卷内快照重名检查 */
static storage_snapshot_rec_t *storage_find_snapshot_in(const char *volume_name,
                                                        const char *snapshot_name) {
    for (storage_snapshot_rec_t *s = g_ctx.snapshots; s; s = s->next) {
        if (!strcmp(s->volume, volume_name) && !strcmp(s->name, snapshot_name)) return s;
    }
    return NULL;
}

static void storage_vol_fill(const storage_volume_rec_t *v, hw_storage_volume_info_t *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->id, sizeof(out->id), "%s", v->id);
    snprintf(out->name, sizeof(out->name), "%s", v->name);
    snprintf(out->path, sizeof(out->path), "%s", v->path);
    snprintf(out->description, sizeof(out->description), "%s", v->description);
    out->size_limit = v->size_limit;
    out->created_at = v->created_at;
    out->readonly = v->readonly;
    struct stat st;
    out->exists = (lstat(v->path, &st) == 0 && S_ISDIR(st.st_mode));
}

static void storage_snap_fill(const storage_snapshot_rec_t *s, hw_storage_snapshot_info_t *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->id, sizeof(out->id), "%s", s->id);
    snprintf(out->name, sizeof(out->name), "%s", s->name);
    snprintf(out->volume, sizeof(out->volume), "%s", s->volume);
    snprintf(out->path, sizeof(out->path), "%s", s->path);
    out->created_at = s->created_at;
    out->active = s->active;
}

/* ============================================================
 * 持久化：storage.state 可读文本（tmp + rename 原子写）
 *
 * 格式（块间空行分隔，每块以记录类型行开始）：
 *   volume
 *   id=<hex>
 *   name=<卷名>
 *   path=<绝对路径>
 *   size_limit=<字节,0=不限>
 *   readonly=<0|1>
 *   created_at=<epoch 秒>
 *   description=<描述，不含换行>
 *
 *   snapshot
 *   id=... name=... volume=<源卷名> path=<快照目录> created_at=... active=1
 * ============================================================ */
static int storage_persist(void) {
    if (g_ctx.state_dir[0] == '\0') return -EINVAL;
    int rc = storage_mkdir_p(g_ctx.state_dir, 0755);
    if (rc != 0) return rc;

    char tmp[STORAGE_PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s/storage.state.tmp", g_ctx.state_dir);
    FILE *fp = fopen(tmp, "w");
    if (!fp) return -errno;

    fprintf(fp, "# HWRun OS STORAGE 注册表状态文件（可读格式，v1）\n");
    fprintf(fp, "# 自动生成，勿手改；块间以空行分隔\n\n");
    for (const storage_volume_rec_t *v = g_ctx.volumes; v; v = v->next) {
        fprintf(fp, "volume\n");
        fprintf(fp, "id=%s\n", v->id);
        fprintf(fp, "name=%s\n", v->name);
        fprintf(fp, "path=%s\n", v->path);
        fprintf(fp, "size_limit=%llu\n", (unsigned long long)v->size_limit);
        fprintf(fp, "readonly=%d\n", v->readonly);
        fprintf(fp, "created_at=%llu\n", (unsigned long long)v->created_at);
        fprintf(fp, "description=%s\n\n", v->description);
    }
    for (const storage_snapshot_rec_t *s = g_ctx.snapshots; s; s = s->next) {
        fprintf(fp, "snapshot\n");
        fprintf(fp, "id=%s\n", s->id);
        fprintf(fp, "name=%s\n", s->name);
        fprintf(fp, "volume=%s\n", s->volume);
        fprintf(fp, "path=%s\n", s->path);
        fprintf(fp, "created_at=%llu\n", (unsigned long long)s->created_at);
        fprintf(fp, "active=%d\n\n", s->active);
    }
    if (fclose(fp) != 0) {
        remove(tmp);
        return -EIO;
    }
    if (rename(tmp, g_ctx.state_file) != 0) {
        int e = -errno;
        remove(tmp);
        return e;
    }
    return 0;
}

/* 从 state_file 整表重载（重启恢复）。缺文件=空表，解析成功返回 0。 */
static int storage_load(void) {
    FILE *fp = fopen(g_ctx.state_file, "r");
    if (!fp) return (errno == ENOENT) ? 0 : -errno;

    enum { REC_NONE, REC_VOL, REC_SNAP } cur = REC_NONE;
    storage_volume_rec_t v;
    storage_snapshot_rec_t s;
    memset(&v, 0, sizeof(v));
    memset(&s, 0, sizeof(s));
    int have = 0;

    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        char *p = storage_strtrim(line);
        if (!*p || *p == '#') continue;
        if (!strcmp(p, "volume")) {
            /* 提交上一条 */
            if (cur == REC_VOL && have) {
                storage_volume_rec_t *n = calloc(1, sizeof(*n));
                if (n) {
                    *n = v;
                    n->next = g_ctx.volumes;
                    g_ctx.volumes = n;
                }
            } else if (cur == REC_SNAP && have) {
                storage_snapshot_rec_t *n = calloc(1, sizeof(*n));
                if (n) {
                    *n = s;
                    n->next = g_ctx.snapshots;
                    g_ctx.snapshots = n;
                }
            }
            cur = REC_VOL;
            memset(&v, 0, sizeof(v));
            have = 0;
            continue;
        }
        if (!strcmp(p, "snapshot")) {
            if (cur == REC_VOL && have) {
                storage_volume_rec_t *n = calloc(1, sizeof(*n));
                if (n) {
                    *n = v;
                    n->next = g_ctx.volumes;
                    g_ctx.volumes = n;
                }
            } else if (cur == REC_SNAP && have) {
                storage_snapshot_rec_t *n = calloc(1, sizeof(*n));
                if (n) {
                    *n = s;
                    n->next = g_ctx.snapshots;
                    g_ctx.snapshots = n;
                }
            }
            cur = REC_SNAP;
            memset(&s, 0, sizeof(s));
            have = 0;
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = p;
        const char *val = eq + 1;
        if (cur == REC_VOL) {
            if (!strcmp(key, "id"))
                snprintf(v.id, sizeof(v.id), "%s", val);
            else if (!strcmp(key, "name"))
                snprintf(v.name, sizeof(v.name), "%s", val);
            else if (!strcmp(key, "path"))
                snprintf(v.path, sizeof(v.path), "%s", val);
            else if (!strcmp(key, "description"))
                snprintf(v.description, sizeof(v.description), "%s", val);
            else if (!strcmp(key, "size_limit"))
                v.size_limit = strtoull(val, NULL, 10);
            else if (!strcmp(key, "readonly"))
                v.readonly = atoi(val);
            else if (!strcmp(key, "created_at"))
                v.created_at = strtoull(val, NULL, 10);
            if (v.id[0] && v.name[0] && v.path[0]) have = 1;
        } else if (cur == REC_SNAP) {
            if (!strcmp(key, "id"))
                snprintf(s.id, sizeof(s.id), "%s", val);
            else if (!strcmp(key, "name"))
                snprintf(s.name, sizeof(s.name), "%s", val);
            else if (!strcmp(key, "volume"))
                snprintf(s.volume, sizeof(s.volume), "%s", val);
            else if (!strcmp(key, "path"))
                snprintf(s.path, sizeof(s.path), "%s", val);
            else if (!strcmp(key, "created_at"))
                s.created_at = strtoull(val, NULL, 10);
            else if (!strcmp(key, "active"))
                s.active = atoi(val);
            if (s.id[0] && s.name[0] && s.volume[0] && s.path[0]) have = 1;
        }
    }
    /* EOF 提交 */
    if (cur == REC_VOL && have) {
        storage_volume_rec_t *n = calloc(1, sizeof(*n));
        if (n) {
            *n = v;
            n->next = g_ctx.volumes;
            g_ctx.volumes = n;
        }
    } else if (cur == REC_SNAP && have) {
        storage_snapshot_rec_t *n = calloc(1, sizeof(*n));
        if (n) {
            *n = s;
            n->next = g_ctx.snapshots;
            g_ctx.snapshots = n;
        }
    }
    fclose(fp);
    return 0;
}

/* ============================================================
 * 卷管理实现
 * ============================================================ */
static int storage_create_volume(const hw_storage_volume_def_t *def,
                                 hw_storage_volume_info_t *out) {
    if (!def || !def->name || !def->path) return -EINVAL;
    if (!g_ctx.initialized) return HWRUN_ENOTREADY;
    int rc = storage_valid_label(def->name, sizeof(((storage_volume_rec_t *)0)->name));
    if (rc != 0) return rc;
    rc = storage_valid_path(def->path);
    if (rc != 0) return rc;

    pthread_mutex_lock(&g_ctx.lock);
    if (storage_find_volume(def->name)) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -EEXIST; /* 卷名已注册 */
    }
    for (storage_volume_rec_t *x = g_ctx.volumes; x; x = x->next) {
        if (!strcmp(x->path, def->path)) {
            pthread_mutex_unlock(&g_ctx.lock);
            return -EEXIST; /* 后端目录已被其它卷占用 */
        }
    }

    /* 确保后端目录存在（用户态注册语义；mkdir 幂等），锁内一并完成防并发建卷 */
    struct stat st;
    int created_dir = 0;
    if (lstat(def->path, &st) != 0) {
        if (errno != ENOENT) {
            int e = -errno;
            pthread_mutex_unlock(&g_ctx.lock);
            return e;
        }
        rc = storage_mkdir_p(def->path, 0755);
        if (rc != 0) {
            pthread_mutex_unlock(&g_ctx.lock);
            return rc;
        }
        created_dir = 1;
    } else if (!S_ISDIR(st.st_mode)) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOTDIR;
    }

    storage_volume_rec_t *rec = calloc(1, sizeof(*rec));
    if (!rec) {
        if (created_dir) rmdir(def->path);
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOMEM;
    }
    storage_gen_id(rec->id, sizeof(rec->id));
    snprintf(rec->name, sizeof(rec->name), "%s", def->name);
    snprintf(rec->path, sizeof(rec->path), "%s", def->path);
    storage_sanitize_desc(rec->description, sizeof(rec->description), def->description);
    rec->size_limit = def->size_limit;
    rec->readonly = def->readonly ? 1 : 0;
    rec->created_at = (uint64_t)time(NULL);

    rec->next = g_ctx.volumes;
    g_ctx.volumes = rec;
    rc = storage_persist();
    if (rc != 0) {
        g_ctx.volumes = rec->next; /* 持久化失败：回滚注册与新建目录 */
        pthread_mutex_unlock(&g_ctx.lock);
        if (created_dir) storage_rm_rf(def->path);
        free(rec);
        return rc;
    }
    if (out) storage_vol_fill(rec, out);
    pthread_mutex_unlock(&g_ctx.lock);
    return 0;
}

static int storage_delete_volume(const char *name_or_id) {
    if (!name_or_id || !name_or_id[0]) return -EINVAL;
    if (!g_ctx.initialized) return HWRUN_ENOTREADY;

    pthread_mutex_lock(&g_ctx.lock);
    storage_volume_rec_t *prev = NULL;
    storage_volume_rec_t *v = g_ctx.volumes;
    while (v && strcmp(v->id, name_or_id) && strcmp(v->name, name_or_id)) {
        prev = v;
        v = v->next;
    }
    if (!v) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOENT;
    }
    if (v->readonly) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -EPERM; /* 只读卷禁删数据 */
    }
    if (!strcmp(v->path, "/")) { /* 防御：绝不递归删除根目录 */
        pthread_mutex_unlock(&g_ctx.lock);
        return -EINVAL;
    }

    /* 存在校验：目录缺失视为数据已失，仅摘元数据；非目录（文件/符号链接）拒绝 */
    struct stat st;
    if (lstat(v->path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            pthread_mutex_unlock(&g_ctx.lock);
            return -ENOTDIR;
        }
        int rc = storage_rm_rf(v->path);
        if (rc != 0) {
            pthread_mutex_unlock(&g_ctx.lock);
            return rc == -1 ? -EIO : rc;
        }
    }

    /* 摘除记录并持久化 */
    if (prev)
        prev->next = v->next;
    else
        g_ctx.volumes = v->next;
    int rc = storage_persist();
    pthread_mutex_unlock(&g_ctx.lock);
    free(v);
    return rc;
}

static int storage_unregister_volume(const char *name_or_id) {
    if (!name_or_id || !name_or_id[0]) return -EINVAL;
    if (!g_ctx.initialized) return HWRUN_ENOTREADY;

    pthread_mutex_lock(&g_ctx.lock);
    storage_volume_rec_t *prev = NULL;
    storage_volume_rec_t *v = g_ctx.volumes;
    while (v && strcmp(v->id, name_or_id) && strcmp(v->name, name_or_id)) {
        prev = v;
        v = v->next;
    }
    if (!v) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOENT;
    }
    if (prev)
        prev->next = v->next;
    else
        g_ctx.volumes = v->next;
    int rc = storage_persist(); /* 仅元数据注销，数据目录原样保留 */
    pthread_mutex_unlock(&g_ctx.lock);
    free(v);
    return rc;
}

static int storage_list_volumes(hw_storage_volume_info_t *out, int cap, int *count) {
    if (!out || !count || cap <= 0) return -EINVAL;
    if (!g_ctx.initialized) return HWRUN_ENOTREADY;

    pthread_mutex_lock(&g_ctx.lock);
    int total = 0, i = 0;
    for (storage_volume_rec_t *v = g_ctx.volumes; v; v = v->next) {
        if (i < cap) storage_vol_fill(v, &out[i++]);
        total++;
    }
    pthread_mutex_unlock(&g_ctx.lock);
    *count = total;
    return 0;
}

static int storage_get_volume(const char *name_or_id, hw_storage_volume_info_t *out) {
    if (!name_or_id || !name_or_id[0] || !out) return -EINVAL;
    if (!g_ctx.initialized) return HWRUN_ENOTREADY;

    pthread_mutex_lock(&g_ctx.lock);
    storage_volume_rec_t *v = storage_find_volume(name_or_id);
    if (!v) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOENT;
    }
    storage_vol_fill(v, out);
    pthread_mutex_unlock(&g_ctx.lock);
    return 0;
}

/* ============================================================
 * 统计：du（真实占用/文件数）+ statvfs（所在文件系统）
 * ============================================================ */
/* nftw 回调无用户数据参数：用静态上下文累积（公开 ops 持锁调用，无并发） */
typedef struct storage_du_ctx {
    uint64_t bytes;
    uint64_t files;
} storage_du_ctx_t;

static storage_du_ctx_t g_du;

static int storage_du_walk_cb(const char *fpath, const struct stat *sb, int tflag,
                              struct FTW *ftw) {
    (void)fpath;
    (void)ftw;
    if (tflag == FTW_F) {
        g_du.bytes += (uint64_t)sb->st_size;
        g_du.files++;
    }
    return 0;
}

static int storage_du(const char *path, uint64_t *bytes, uint64_t *files) {
    g_du.bytes = 0;
    g_du.files = 0;
    int rc = nftw(path, storage_du_walk_cb, 64, FTW_PHYS);
    if (rc != 0) return rc == -1 ? -errno : rc;
    if (bytes) *bytes = g_du.bytes;
    if (files) *files = g_du.files;
    return 0;
}

static int storage_volume_df(const char *name_or_id, hw_storage_df_t *out) {
    if (!name_or_id || !name_or_id[0] || !out) return -EINVAL;
    if (!g_ctx.initialized) return HWRUN_ENOTREADY;

    pthread_mutex_lock(&g_ctx.lock);
    storage_volume_rec_t *v = storage_find_volume(name_or_id);
    if (!v) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOENT;
    }
    struct stat st;
    if (lstat(v->path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOENT;
    }
    memset(out, 0, sizeof(*out));
    out->size_limit = v->size_limit;
    int rc = storage_du(v->path, &out->used_bytes, &out->file_count);
    if (rc != 0) {
        pthread_mutex_unlock(&g_ctx.lock);
        return rc;
    }
    struct statvfs sv;
    if (statvfs(v->path, &sv) != 0) {
        rc = -errno;
        pthread_mutex_unlock(&g_ctx.lock);
        return rc;
    }
    unsigned long frsize = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
    out->fs_block_size = (uint64_t)sv.f_bsize;
    out->fs_total = (uint64_t)sv.f_blocks * (uint64_t)frsize;
    out->fs_used = (uint64_t)(sv.f_blocks - sv.f_bfree) * (uint64_t)frsize;
    out->fs_avail = (uint64_t)sv.f_bavail * (uint64_t)frsize;
    out->fs_files = (uint64_t)sv.f_files;
    out->fs_files_free = (uint64_t)sv.f_ffree;
    pthread_mutex_unlock(&g_ctx.lock);
    return 0;
}

/* ============================================================
 * 快照实现（目录级真实副本）
 * ============================================================ */
static int storage_snapshot_create(const char *name_or_id, const char *snapshot_name,
                                   hw_storage_snapshot_info_t *out) {
    if (!name_or_id || !name_or_id[0] || !snapshot_name) return -EINVAL;
    if (!g_ctx.initialized) return HWRUN_ENOTREADY;
    int rc = storage_valid_label(snapshot_name, sizeof(((storage_snapshot_rec_t *)0)->name));
    if (rc != 0) return rc;

    /* 第一段锁：定位源卷、推导快照路径、拒绝目录已存在 */
    char vol_name[128];
    char vol_path[STORAGE_PATH_MAX];
    char snap_path[STORAGE_PATH_MAX];
    pthread_mutex_lock(&g_ctx.lock);
    storage_volume_rec_t *v = storage_find_volume(name_or_id);
    if (!v) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOENT;
    }
    struct stat st;
    if (lstat(v->path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOENT;
    }
    if (storage_find_snapshot_in(v->name, snapshot_name)) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -EEXIST;
    }
    snprintf(vol_name, sizeof(vol_name), "%s", v->name);
    snprintf(vol_path, sizeof(vol_path), "%s", v->path);
    snprintf(snap_path, sizeof(snap_path), "%s/%s/%s/%s", g_ctx.state_dir, STORAGE_SNAP_ROOT,
             v->name, snapshot_name);
    if (lstat(snap_path, &st) == 0) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -EEXIST; /* 快照目录已存在 */
    }
    pthread_mutex_unlock(&g_ctx.lock);

    /* 真实复制（锁外执行 IO；快照区仅本插件写，路径已校验） */
    rc = storage_copy_tree(vol_path, snap_path);
    if (rc != 0) {
        storage_rm_rf(snap_path); /* 失败不留残件 */
        return rc;
    }

    storage_snapshot_rec_t *s = calloc(1, sizeof(*s));
    if (!s) {
        storage_rm_rf(snap_path);
        return -ENOMEM;
    }
    storage_gen_id(s->id, sizeof(s->id));
    snprintf(s->name, sizeof(s->name), "%s", snapshot_name);
    snprintf(s->volume, sizeof(s->volume), "%s", vol_name);
    snprintf(s->path, sizeof(s->path), "%s", snap_path);
    s->created_at = (uint64_t)time(NULL);
    s->active = 1;

    /* 第二段锁：源卷被并发注销或快照重名时放弃本次产物 */
    pthread_mutex_lock(&g_ctx.lock);
    if (!storage_find_volume(vol_name)) {
        pthread_mutex_unlock(&g_ctx.lock);
        storage_rm_rf(snap_path);
        free(s);
        return -ENOENT;
    }
    if (storage_find_snapshot_in(vol_name, snapshot_name)) {
        pthread_mutex_unlock(&g_ctx.lock);
        storage_rm_rf(snap_path);
        free(s);
        return -EEXIST;
    }
    s->next = g_ctx.snapshots;
    g_ctx.snapshots = s;
    rc = storage_persist();
    if (rc != 0) {
        g_ctx.snapshots = s->next;
        pthread_mutex_unlock(&g_ctx.lock);
        storage_rm_rf(snap_path);
        free(s);
        return rc;
    }
    pthread_mutex_unlock(&g_ctx.lock);

    if (out) storage_snap_fill(s, out);
    return 0;
}

static int storage_snapshot_restore(const char *snapshot_name, const char *target_volume) {
    if (!snapshot_name || !snapshot_name[0]) return -EINVAL;
    if (!g_ctx.initialized) return HWRUN_ENOTREADY;

    char snap_path[STORAGE_PATH_MAX];
    char vol_path[STORAGE_PATH_MAX];
    pthread_mutex_lock(&g_ctx.lock);
    storage_snapshot_rec_t *s = storage_find_snapshot(snapshot_name);
    if (!s) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOENT;
    }
    const char *want = target_volume && target_volume[0] ? target_volume : s->volume;
    storage_volume_rec_t *v = storage_find_volume(want);
    if (!v) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOENT;
    }
    if (v->readonly) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -EPERM; /* 只读卷拒绝恢复写入 */
    }
    struct stat st;
    if (lstat(s->path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOENT;
    }
    if (lstat(v->path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOENT;
    }
    snprintf(snap_path, sizeof(snap_path), "%s", s->path);
    snprintf(vol_path, sizeof(vol_path), "%s", v->path);
    pthread_mutex_unlock(&g_ctx.lock);

    /* 恢复：清空目标卷目录 → 把快照内容整体复制回卷目录（真实字节副本） */
    int rc = storage_dir_clear(vol_path);
    if (rc != 0) return rc;
    rc = storage_copy_tree(snap_path, vol_path);
    if (rc != 0) return rc;
    return 0;
}

static int storage_snapshot_delete(const char *snapshot_name) {
    if (!snapshot_name || !snapshot_name[0]) return -EINVAL;
    if (!g_ctx.initialized) return HWRUN_ENOTREADY;

    pthread_mutex_lock(&g_ctx.lock);
    storage_snapshot_rec_t *prev = NULL;
    storage_snapshot_rec_t *s = g_ctx.snapshots;
    while (s && strcmp(s->id, snapshot_name) && strcmp(s->name, snapshot_name)) {
        prev = s;
        s = s->next;
    }
    if (!s) {
        pthread_mutex_unlock(&g_ctx.lock);
        return -ENOENT;
    }
    if (prev)
        prev->next = s->next;
    else
        g_ctx.snapshots = s->next;
    int rc = storage_persist();
    pthread_mutex_unlock(&g_ctx.lock);
    /* 快照目录若仍在则递归删除（缺失=数据已失，仍视为删除成功） */
    struct stat st;
    if (rc == 0 && lstat(s->path, &st) == 0 && S_ISDIR(st.st_mode)) {
        int drc = storage_rm_rf(s->path);
        if (drc != 0) rc = (drc == -1) ? -EIO : drc;
    }
    free(s);
    return rc;
}

static int storage_list_snapshots(const char *volume, hw_storage_snapshot_info_t *out, int cap,
                                  int *count) {
    if (!out || !count || cap <= 0) return -EINVAL;
    if (!g_ctx.initialized) return HWRUN_ENOTREADY;

    pthread_mutex_lock(&g_ctx.lock);
    int total = 0, i = 0;
    for (storage_snapshot_rec_t *s = g_ctx.snapshots; s; s = s->next) {
        if (volume && volume[0] && strcmp(s->volume, volume)) continue;
        if (i < cap) storage_snap_fill(s, &out[i++]);
        total++;
    }
    pthread_mutex_unlock(&g_ctx.lock);
    *count = total;
    return 0;
}

/* ============================================================
 * 生命周期（storage_core.c 经 storage_impl_init/shutdown 调用）
 * ============================================================ */
int storage_impl_init(void) {
    memset(&g_ctx, 0, sizeof(g_ctx));
    if (pthread_mutex_init(&g_ctx.lock, NULL) != 0) return -EIO;

    const char *env = getenv("HWRUN_STATE");
    if (env && env[0]) {
        if (strlen(env) >= STORAGE_DIR_MAX) {
            pthread_mutex_destroy(&g_ctx.lock);
            return -ENAMETOOLONG; /* 状态目录过长会使快照子路径溢出 512 字段 */
        }
        snprintf(g_ctx.state_dir, sizeof(g_ctx.state_dir), "%s", env);
    } else {
        snprintf(g_ctx.state_dir, sizeof(g_ctx.state_dir), "%s", "/var/lib/hwrun");
    }
    snprintf(g_ctx.state_file, sizeof(g_ctx.state_file), "%s/%s", g_ctx.state_dir,
             STORAGE_STATE_FILE);

    int rc = storage_load(); /* 重启恢复：读入既有卷/快照注册表 */
    if (rc != 0) {
        pthread_mutex_destroy(&g_ctx.lock);
        return rc;
    }
    g_ctx.initialized = 1;
    return 0;
}

void storage_impl_shutdown(void) {
    if (!g_ctx.initialized && !g_ctx.volumes && !g_ctx.snapshots) return;
    storage_volume_rec_t *v = g_ctx.volumes;
    while (v) {
        storage_volume_rec_t *n = v->next;
        free(v);
        v = n;
    }
    storage_snapshot_rec_t *s = g_ctx.snapshots;
    while (s) {
        storage_snapshot_rec_t *n = s->next;
        free(s);
        s = n;
    }
    g_ctx.volumes = NULL;
    g_ctx.snapshots = NULL;
    g_ctx.initialized = 0;
    pthread_mutex_destroy(&g_ctx.lock);
}

/* ============================================================
 * ops 表
 * ============================================================ */
static int32_t storage_version(void) {
    return 1;
}

hw_storage_ops_t hw_storage_ops = {
    .version = storage_version,
    .create_volume = storage_create_volume,
    .delete_volume = storage_delete_volume,
    .unregister_volume = storage_unregister_volume,
    .list_volumes = storage_list_volumes,
    .get_volume = storage_get_volume,
    .volume_df = storage_volume_df,
    .snapshot_create = storage_snapshot_create,
    .snapshot_restore = storage_snapshot_restore,
    .snapshot_delete = storage_snapshot_delete,
    .list_snapshots = storage_list_snapshots,
};

#ifdef __cplusplus
}
#endif
