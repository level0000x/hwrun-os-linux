/*
 * fsp_mount.c — FSP 挂载点枚举
 *
 * 读取 /proc/mounts（MSYS2 / Linux 原生均可用），解析为 fsp_mountinfo_t。
 * mount/umount 因涉及系统级权限与命名空间,在此仅提供只读枚举接口；
 * 实际挂载/卸载上层可基于本枚举结果 + 系统 mount(8)/umount(8) 完成。
 */

#include "fsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* 逐字段处理挂载行中的 \040(空格) 转义，还原真实字符 */
static void unescape(const char *in, char *out, size_t cap) {
    size_t n = 0;
    for (const char *p = in; *p && n + 1 < cap; p++) {
        if (*p == '\\' && p[1] == '0' && p[2] == '4' && p[3] == '0') {
            out[n++] = ' ';
            p += 3;
        } else {
            out[n++] = *p;
        }
    }
    out[n] = '\0';
}

static int fsp_mount_list(fsp_mountinfo_t *infos, int max) {
    if (!infos || max <= 0) return -EINVAL;

    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) return -errno;

    char line[1024];
    int count = 0;
    while (fgets(line, sizeof(line), fp) && count < max) {
        char dev[256], mnt[256], fstype[64], opts[256];
        unsigned freq = 0, passno = 0;

        /* 格式：device mountpoint fstype options freq passno（空格分隔） */
        char *d = strtok(line, " \t\n");
        char *m = strtok(NULL, " \t\n");
        char *f = strtok(NULL, " \t\n");
        char *o = strtok(NULL, " \t\n");
        char *fr = strtok(NULL, " \t\n");
        char *ps = strtok(NULL, " \t\n");
        if (!d || !m || !f || !o || !fr || !ps) continue;

        unescape(d, dev, sizeof(dev));
        unescape(m, mnt, sizeof(mnt));
        snprintf(fstype, sizeof(fstype), "%s", f);
        snprintf(opts, sizeof(opts), "%s", o);
        /* freq/passno 仅 /etc/fstab 有值，mounts 通常为 0 0 */
        freq = (unsigned)strtoul(fr, NULL, 10);
        passno = (unsigned)strtoul(ps, NULL, 10);

        fsp_mountinfo_t *mi = &infos[count];
        memset(mi, 0, sizeof(*mi));
        snprintf(mi->device, sizeof(mi->device), "%s", dev);
        snprintf(mi->mountpoint, sizeof(mi->mountpoint), "%s", mnt);
        snprintf(mi->fstype, sizeof(mi->fstype), "%s", fstype);
        snprintf(mi->options, sizeof(mi->options), "%s", opts);
        mi->freq = freq;
        mi->passno = passno;
        count++;
    }
    fclose(fp);
    return count;
}

void hw_fsp_ops_mount_init(hw_fsp_ops_t *ops) {
    ops->mount_list = fsp_mount_list;
}