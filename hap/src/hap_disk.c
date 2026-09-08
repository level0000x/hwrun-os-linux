/*
 * hap_disk.c — 磁盘设备信息
 *
 * Linux 下：
 *   - 底层块设备：遍历 /sys/block，读取 /sys/block/<dev>/size（扇区 512B）
 *     得到总容量；型号读 /sys/block/<dev>/device/model，串行读 /sys/block/<dev>/serial。
 *   - 分区使用情况：遍历 /proc/mounts，统计 该设备名/挂载点 的已用/可用。
 * 降级环境下 count=0，绝不崩溃。
 */

#include "../include/hap.h"
#include "hap_util.h"

#include <dirent.h>
#include <sys/statvfs.h>

#define SYS_BLOCK "/sys/block"
#define SYS_DEV_MODEL "%s/%s/device/model"
#define SYS_DEV_SERIAL "%s/%s/device/serial"
#define SYS_DEV_SIZE "%s/%s/size"
#define PROC_MOUNTS "/proc/mounts"

/* 简单判断挂载源是否命中块设备名（如 /dev/sda1 命中 sda） */
static int hap_mount_matches(const char *fsdev, const char *diskname) {
    size_t n = strlen(diskname);
    if (!fsdev || !diskname) return 0;
    if (strncmp(fsdev, "/dev/", 5) != 0) return 0;
    return strncmp(fsdev + 5, diskname, n) == 0;
}

/* 从 statfs/路径读取挂载点使用情况；返回 0 表示命中并填充 */
static int hap_stat_mount(const char *mp, uint64_t *used_mb, uint64_t *free_mb, double *pct) {
#if defined(_WIN32)
    return HAP_USERES;
#else
    struct statvfs st;
    unsigned long blksize;
    uint64_t total, used, avail;
    if (statvfs(mp, &st) != 0 || st.f_blocks == 0) return HAP_USERES;
    blksize = (unsigned long)st.f_frsize ? (unsigned long)st.f_frsize : (unsigned long)st.f_bsize;
    total = (uint64_t)st.f_blocks * blksize;
    avail = (uint64_t)st.f_bavail * blksize;
    used = (total > avail) ? (total - avail) : 0;
    *used_mb = used / (1024ULL * 1024ULL);
    *free_mb = avail / (1024ULL * 1024ULL);
    *pct = total ? ((double)used / (double)total) * 100.0 : 0.0;
    return HAP_OK;
#endif
}

int hap_disk_probe(hap_disk_info_t *out) {
    struct dirent *e;
    DIR *d;
    char path[512], val[256];
    uint32_t idx = 0;

    if (!out) return HAP_USERES;
    memset(out, 0, sizeof(hap_disk_info_t));

    d = opendir(SYS_BLOCK);
    if (!d) return HAP_OK; /* 无 /sys/block（如 Windows） */

    while ((e = readdir(d)) != NULL && idx < sizeof(out->devices) / sizeof(out->devices[0])) {
        /* 跳过虚拟设备与指针链接目录 */
        if (e->d_name[0] == '.') continue;
        /* 仅统计真实可容量设备：既非 loop/ram 分区，又有 size 文件 */
        snprintf(path, sizeof(path), SYS_DEV_SIZE, SYS_BLOCK, e->d_name);
        if (hap_read_file(path, val, sizeof(val)) == 0) continue;

        hap_disk_device_t *dev = &out->devices[idx];
        memset(dev, 0, sizeof(*dev));
        strncpy(dev->name, e->d_name, sizeof(dev->name) - 1);

        /* 容量 = 扇区数 * 512 字节 */
        {
            unsigned long long sectors = strtoull(val, NULL, 10);
            dev->capacity_mb = (uint64_t)((sectors * 512ULL) / (1024ULL * 1024ULL));
        }

        /* 型号与串口（可为空，降级不报错） */
        snprintf(path, sizeof(path), SYS_DEV_MODEL, SYS_BLOCK, e->d_name);
        if (hap_read_file(path, val, sizeof(val)) > 0) {
            char *p = val + strlen(val);
            while (p > val && isspace((unsigned char)p[-1])) {
                p--;
                *p = '\0';
            }
            strncpy(dev->model, val, sizeof(dev->model) - 1);
        }
        snprintf(path, sizeof(path), SYS_DEV_SERIAL, SYS_BLOCK, e->d_name);
        if (hap_read_file(path, val, sizeof(val)) > 0) {
            char *p = val + strlen(val);
            while (p > val && isspace((unsigned char)p[-1])) {
                p--;
                *p = '\0';
            }
        }

        dev->available = 1;
        idx++;
    }
    closedir(d);

    /* 挂载点使用情况：扫描 /proc/mounts，匹配设备前缀的块设备 */
    if (idx > 0) {
        char mounts[8192];
        if (hap_read_file(PROC_MOUNTS, mounts, sizeof(mounts)) > 0) {
            const char *p = mounts;
            while (p && *p) {
                char fsdev[256] = "", mp[256] = "", fstype[64] = "";
                const char *nl = strchr(p, '\n');
                size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
                char *line = (char *)alloca(linelen + 1);
                memcpy(line, p, linelen);
                line[linelen] = '\0';
                /* 格式：device mountpoint fstype opts dump pass */
                {
                    char *t = line;
                    char *sp;
                    /* device */
                    sp = strchr(t, ' ');
                    if (sp) {
                        *sp = '\0';
                        snprintf(fsdev, sizeof(fsdev), "%s", t);
                        t = sp + 1;
                    }
                    /* mountpoint */
                    sp = strchr(t, ' ');
                    if (sp) {
                        *sp = '\0';
                        snprintf(mp, sizeof(mp), "%s", t);
                        t = sp + 1;
                    }
                    /* fstype */
                    sp = strchr(t, ' ');
                    if (sp) {
                        *sp = '\0';
                        snprintf(fstype, sizeof(fstype), "%s", t);
                    }
                }
                if (mp[0]) {
                    for (uint32_t i = 0; i < idx; i++) {
                        hap_disk_device_t *dev = &out->devices[i];
                        if (hap_mount_matches(fsdev, dev->name) && dev->mount_point[0] == '\0') {
                            uint64_t used_mb = 0, free_mb = 0;
                            double pct = 0.0;
                            if (hap_stat_mount(mp, &used_mb, &free_mb, &pct) == HAP_OK) {
                                strncpy(dev->mount_point, mp, sizeof(dev->mount_point) - 1);
                                strncpy(dev->fs_type, fstype, sizeof(dev->fs_type) - 1);
                                dev->used_mb = used_mb;
                                dev->free_mb = free_mb;
                                dev->use_pct = pct;
                            }
                            break;
                        }
                    }
                }
                if (!nl) break;
                p = nl + 1;
            }
        }
    }

    out->device_count = idx;
    return HAP_OK;
}