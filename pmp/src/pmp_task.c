/*
 * pmp_task.c — 任务（task）管理：提交 / 取消 / 状态查询
 *
 * 任务 = PMP 高层抽象：把 spawn 出的子进程登记进内部任务表，
 * 提供统一的任务 ID 句柄，便于上层插件（SCHED/SHELL）跟踪。
 * 后台进程通过 SIGCHLD 收集，状态惰性更新（查询时清理僵尸）。
 */

#include "pmp.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>

/* pmp_proc.c 提供的 spawn 封装（fork+exec / CreateProcess 兜底） */
extern int pmp_spawn(const char *path, char * const argv[], int *out_pid);

#define PMP_TASK_TABLE_MAX   1024

/* 内部任务表条目 */
typedef struct pmp_task_entry {
    int             used;
    uint64_t        id;               /* 全局唯一任务 ID       */
    char            name[64];
    char            path[256];
    int32_t         pid;              /* 操作系统 PID          */
    pmp_task_status_t state;          /* 任务状态              */
    int             exit_status;      /* 退出状态              */
    struct pmp_task_entry *next;
} pmp_task_entry_t;

static pmp_task_entry_t *g_task_table = NULL;
static uint64_t g_task_seq = 0;

/* 内部：创建/销毁任务表头 */
void pmp_task_deinit(void) {
    pmp_task_entry_t *e = g_task_table, *nx;
    while (e) { nx = e->next; free(e); e = nx; }
    g_task_table = NULL;
}

/* 内部：按 id 查找 */
static pmp_task_entry_t *task_find(uint64_t id) {
    for (pmp_task_entry_t *e = g_task_table; e; e = e->next)
        if (e->used && e->id == id) return e;
    return NULL;
}

/* 内部：惰性收集已结束子进程，更新任务状态 */
static void task_reap(void) {
    int st;
    pid_t r;
    errno = 0;
    /* 尽量回收一个 */
    r = waitpid(-1, &st, WNOHANG);
    if (r <= 0) return;
    for (pmp_task_entry_t *e = g_task_table; e; e = e->next) {
        if (e->used && (int32_t)r == e->pid) {
            e->state = (WIFEXITED(st) && WEXITSTATUS(st) == 0)
                           ? PMP_TASK_STATUS_FINISHED : PMP_TASK_STATUS_FAILED;
            e->exit_status = WIFEXITED(st) ? WEXITSTATUS(st)
                                           : (WIFSIGNALED(st) ? WTERMSIG(st) : -1);
            break;
        }
    }
}

/* ============================================================
 * 提交任务：后台运行 path，返回任务 ID
 * ============================================================ */
uint64_t pmp_task_submit(const char *name, const char *path,
                         char * const argv[], int *out_err) {
    int pid;
    if (!path) { if (out_err) *out_err = -1; return 0; }

    int rc = pmp_spawn(path, argv, &pid);
    if (rc != 0 || pid < 0) {
        if (out_err) *out_err = rc;
        return 0;
    }

    /* 登记进任务表 */
    pmp_task_entry_t *e = (pmp_task_entry_t *)calloc(1, sizeof(*e));
    if (!e) { if (out_err) *out_err = -3; return 0; }
    e->used = 1;
    e->id = ++g_task_seq;
    e->pid = (int32_t)pid;
    e->state = PMP_TASK_STATUS_RUNNING;
    e->exit_status = 0;
    if (name) snprintf(e->name, sizeof(e->name), "%s", name);
    else      snprintf(e->name, sizeof(e->name), "%s", path);
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->next = g_task_table;
    g_task_table = e;

    if (out_err) *out_err = 0;
    return e->id;
}

int pmp_task_cancel(uint64_t task_id) {
    pmp_task_entry_t *e = task_find(task_id);
    if (!e) return -2;                 /* HWRUN_ENOENT */
    if (e->state != PMP_TASK_STATUS_RUNNING && e->state != PMP_TASK_STATUS_PENDING)
        return -1;                     /* HWRUN_EINVAL（已结束） */
    if (e->pid > 0) {
        kill((pid_t)e->pid, SIGTERM);
        /* 轻微等待，避免立即回收（非阻塞） */
    }
    e->state = PMP_TASK_STATUS_CANCELLED;
    return 0;
}

int pmp_task_status(uint64_t task_id, pmp_task_info_t *out) {
    pmp_task_entry_t *e = task_find(task_id);
    if (!e) { if (out) memset(out, 0, sizeof(*out)); return -2; }
    task_reap();                        /* 惰性回收 */
    if (out) {
        memset(out, 0, sizeof(*out));
        out->id = e->id;
        snprintf(out->name, sizeof(out->name), "%s", e->name);
        snprintf(out->path, sizeof(out->path), "%s", e->path);
        out->pid = e->pid;
        out->state = e->state;
        out->exit_status = e->exit_status;
    }
    return 0;
}