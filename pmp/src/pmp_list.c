/*
 * pmp_list.c — 进程列表 / 进程信息
 *
 * 基于 /proc 枚举与查询。在 MSYS2 / 无 /proc 环境下优雅降级：
 * 返回非零错误码，不崩溃。
 */

#include "pmp.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

/* 进程状态字母 -> pmp 枚举 */
static pmp_task_state_t map_state(char c) {
    switch (c) {
    case 'R': return PMP_TASK_RUNNING;
    case 'S': return PMP_TASK_INTERRUPTIBLE;
    case 'D': return PMP_TASK_UNINTERRUPTIBLE;
    case 'Z': return PMP_TASK_ZOMBIE;
    case 'T': return PMP_TASK_STOPPED;
    case 't': return PMP_TASK_TRACING;
    case 'X': case 'x': return PMP_TASK_DEAD;
    case 'K': case 'W': case 'P': case 'I': return PMP_TASK_WAKEKILL;
    default:  return PMP_TASK_UNKNOWN;
    }
}

/*
 * 解析 /proc/<pid>/stat。
 * 字段（注意 field 2 的 comm 可能含空格/括号）：
 *  1 pid  2 (comm)  3 state  4 ppid  5 pgrp  ... 14 utime 15 stime 22 starttime
 */
static void parse_stat(const char *buf, pmp_process_info_t *info) {
    /* comm 起点 = 第一个 '(' 后，终点 = 最后一个 ')' */
    const char *lp = strchr(buf, '(');
    const char *rp = lp ? strrchr(buf, ')') : NULL;
    size_t n = (lp && rp && rp > lp) ? (size_t)(rp - lp - 1) : 0;
    if (n >= sizeof(info->comm)) n = sizeof(info->comm) - 1;
    memset(info->comm, 0, sizeof(info->comm));
    if (n > 0 && lp) {
        memcpy(info->comm, lp + 1, n);
        info->comm[n] = '\0';
    }

    /* 之后是：pid state ppid ... */
    if (lp) info->pid = (uint32_t)atoi(buf);
    const char *p = rp ? rp + 1 : buf;
    /* 跳过空白 */
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) return;
    char state_ch = *p;
    info->state = map_state(state_ch);
    while (*p && !isspace((unsigned char)*p)) p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p) info->ppid = (uint32_t)atoi(p);      /* field 4 ppid */
}

/* 解析 /proc/<pid>/statm：第一列虚拟页，第二列常驻页 */
static void parse_statm(int pid, uint64_t *rss_kb, uint64_t *virt_kb) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/statm", pid);
    FILE *f = fopen(path, "r");
    if (!f) return;
    unsigned long vsz = 0, rss = 0;
    if (fscanf(f, "%lu %lu", &vsz, &rss) == 2) {
        long page = sysconf(_SC_PAGESIZE);
        if (page <= 0) page = 4096;
        if (virt_kb) *virt_kb = (uint64_t)vsz * (uint64_t)page / 1024;
        if (rss_kb)  *rss_kb  = (uint64_t)rss * (uint64_t)page / 1024;
    }
    fclose(f);
}

/* 读取 /proc/<pid>/sched 中的调度策略文本（尽力而为，失败回退 OTHER） */
static pmp_sched_policy_t read_policy(int pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/sched", pid);
    FILE *f = fopen(path, "r");
    if (!f) return PMP_SCHED_OTHER;
    char line[128];
    pmp_sched_policy_t policy = PMP_SCHED_OTHER;
    /* 第二行形如：# (C, #threads: 1) 或策略名 */
    if (fgets(line, sizeof(line), f) && fgets(line, sizeof(line), f)) {
        if (strstr(line, "SCHED_FIFO"))      policy = PMP_SCHED_FIFO;
        else if (strstr(line, "SCHED_RR"))   policy = PMP_SCHED_RR;
        else if (strstr(line, "SCHED_BATCH"))policy = PMP_SCHED_BATCH;
        else if (strstr(line, "SCHED_IDLE")) policy = PMP_SCHED_IDLE;
        else if (strstr(line, "SCHED_DEADLINE")) policy = PMP_SCHED_DEADLINE;
    }
    fclose(f);
    return policy;
}

/* 填充单个进程的全部信息 */
static int fill_info(int pid, pmp_process_info_t *info) {
    char path[64];
    char buf[1024];
    memset(info, 0, sizeof(*info));

    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -errno;
    if (fgets(buf, sizeof(buf), f) == NULL) { fclose(f); return -errno; }
    fclose(f);

    info->pid = (uint32_t)pid;
    info->tgid = (uint32_t)pid;
    parse_stat(buf, info);
    parse_statm(pid, &info->memory_rss, &info->memory_virtual);
    info->policy = read_policy(pid);

    /* 亲和性掩码：读取可从 /proc/<pid>/status 的 Cpus_allowed_list 或直接取最低 CPU */
    info->cpu_affinity = 0;
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *st = fopen(path, "r");
    if (st) {
        char l[256];
        while (fgets(l, sizeof(l), st)) {
            if (strncmp(l, "Cpus_allowed_list:", 18) == 0) {
                /* 取第一个 CPU 号 */
                const char *v = l + 18;
                while (*v && !isdigit((unsigned char)*v)) v++;
                if (isdigit((unsigned char)*v))
                    info->cpu_affinity = (uint32_t)atoi(v);
                break;
            }
        }
        fclose(st);
    }

    info->priority = 0;
    info->nice = 0;
    return 0;
}

/* ============================================================
 * 获取进程列表：遍历 /proc 下数字目录
 * ============================================================ */
int pmp_get_process_list(pmp_process_info_t **out_list, uint32_t *out_count) {
    if (!out_list || !out_count) return -1;   /* HWRUN_EINVAL */

    DIR *dir = opendir("/proc");
    if (!dir) return -errno;                  /* 无 /proc，优雅降级 */

    uint32_t cap = 64, count = 0;
    pmp_process_info_t *list =
        (pmp_process_info_t *)calloc(cap, sizeof(*list));
    if (!list) { closedir(dir); return -3; }  /* HWRUN_ENOMEM */

    struct dirent *ent;
    errno = 0;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] < '0' || name[0] > '9') continue;   /* 数字目录才是进程 */
        int pid = atoi(name);
        if (pid <= 0) continue;
        if (count >= cap) {
            pmp_process_info_t *nl = (pmp_process_info_t *)
                realloc(list, (cap * 2) * sizeof(*list));
            if (!nl) break;                  /* 空间不足，交付已收集部分 */
            list = nl; cap *= 2;
        }
        if (fill_info(pid, &list[count]) == 0)
            count++;
    }
    closedir(dir);

    *out_list = list;
    *out_count = count;
    return 0;                                 /* HWRUN_OK */
}

void pmp_free_process_list(pmp_process_info_t *list, uint32_t count) {
    (void)count;
    free(list);
}

int pmp_get_process_info(int32_t pid, pmp_process_info_t *out_info) {
    if (!out_info || pid <= 0) return -1;     /* HWRUN_EINVAL */
    return fill_info(pid, out_info);
}

int pmp_get_current_pid(void) {
    return (int)getpid();
}