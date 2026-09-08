/*
 * pmp_proc.c — 进程创建 / 控制 / 调度 / 亲和性
 *
 * Linux/POSIX 实现：fork / execve / waitpid / kill / sigstop / sched_*.
 * MSYS2 环境：用 POSIX syscall；在真正无 fork 的 Windows(MinGW, _WIN32)
 * 环境下，spawn 走 CreateProcess 兜底，fork 优雅返回错误码，绝不崩溃。
 */

#define _GNU_SOURCE
#include "pmp.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/resource.h>

#if defined(_WIN32)
#include <windows.h>
#include <process.h>
#endif

/* ============================================================
 * 调度策略：pmp 枚举 <-> Linux sched 常量 映射
 * ============================================================ */
static int pmp_to_linux_policy(pmp_sched_policy_t p) {
#ifdef SCHED_DEADLINE
    switch (p) {
    case PMP_SCHED_OTHER:    return SCHED_OTHER;
    case PMP_SCHED_FIFO:     return SCHED_FIFO;
    case PMP_SCHED_RR:       return SCHED_RR;
    case PMP_SCHED_BATCH:    return SCHED_BATCH;
    case PMP_SCHED_IDLE:     return SCHED_IDLE;
    case PMP_SCHED_DEADLINE: return SCHED_DEADLINE;
    }
#else
    (void)p;
#endif
    return SCHED_OTHER;
}

static pmp_sched_policy_t linux_to_pmp_policy(int lp) {
    switch (lp) {
    case SCHED_OTHER: return PMP_SCHED_OTHER;
    case SCHED_FIFO:  return PMP_SCHED_FIFO;
    case SCHED_RR:    return PMP_SCHED_RR;
    case SCHED_BATCH: return PMP_SCHED_BATCH;
#ifdef SCHED_IDLE
    case SCHED_IDLE:  return PMP_SCHED_IDLE;
#endif
#ifdef SCHED_DEADLINE
    case SCHED_DEADLINE: return PMP_SCHED_DEADLINE;
#endif
    default:          return PMP_SCHED_OTHER;
    }
}

/* ============================================================
 * 进程创建
 * ============================================================ */
int pmp_fork(void) {
#if defined(_WIN32)
    /* Windows 无 fork，优雅返回不支持 */
    return -ENOTSUP;
#else
    return (int)fork();
#endif
}

int pmp_exec(const char *path, char * const argv[]) {
    if (!path) return -EINVAL;
    /* exec 成功不返回；失败用 errno */
    execv(path, argv);
    return -errno;
}

int pmp_execve_full(const char *path, char * const argv[], char * const envp[]) {
    if (!path) return -EINVAL;
#if defined(_WIN32)
    (void)argv; (void)envp;
    return -ENOTSUP;
#else
    execve(path, argv, envp);
    return -errno;
#endif
}

/* spawn：fork + exec 封装 */
int pmp_spawn(const char *path, char * const argv[], int *out_pid) {
    if (!path || !out_pid) return -EINVAL;
#if defined(_WIN32)
    (void)argv;
    /* CreateProcess 兜底 */
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    char cmd[2048];
    {
        int l = snprintf(cmd, sizeof(cmd), "\"%s\"", path);
        if (argv && argv[0]) {
            for (int i = 1; argv[i] && l < (int)sizeof(cmd) - 2; i++)
                l += snprintf(cmd + l, sizeof(cmd) - (size_t)l, " %s", argv[i]);
        }
    }
    if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE, 0,
                       NULL, NULL, &si, &pi)) {
        *out_pid = -1;
        return -errno;
    }
    *out_pid = (int)pi.dwProcessId;
    /* 父侧句柄由任务表持有；此处复用于任务管理时不应重复关闭 */
    CloseHandle(pi.hThread);
    return 0;
#else
    pid_t pid = fork();
    if (pid < 0) { *out_pid = -1; return -errno; }
    if (pid == 0) {
        /* 子进程：直接 exec；失败则退出，避免污染调用者 */
        execv(path, argv);
        _exit(127);
    }
    *out_pid = (int)pid;
    return 0;
#endif
}

/* ============================================================
 * 等待 / 信号 / 启停
 * ============================================================ */
int pmp_wait(int32_t pid, int *status, int options) {
    int st;
    pid_t r = waitpid((pid_t)pid, &st, options);
    if (r < 0) return -errno;
    if (status) *status = st;
    return (int)r;
}

int pmp_signal(int32_t pid, int sig) {
    if (kill((pid_t)pid, sig) < 0) return -errno;
    return 0;
}

int pmp_kill(int32_t pid, int sig) {
    return pmp_signal(pid, sig);
}

int pmp_start(int32_t pid)   { return pmp_signal(pid, SIGCONT); }
int pmp_stop(int32_t pid)    { return pmp_signal(pid, SIGSTOP); }
int pmp_pause_process(int32_t pid) { return pmp_signal(pid, SIGSTOP); }
int pmp_resume(int32_t pid)  { return pmp_signal(pid, SIGCONT); }

/* ============================================================
 * 调度
 * ============================================================ */
int pmp_sched_set(int32_t pid, pmp_sched_policy_t policy,
                  int32_t priority, int32_t nice) {
#if defined(_WIN32)
    (void)pid; (void)policy; (void)priority; (void)nice;
    return -5;                    /* HWRUN_ENOTSUP */
#else
    int lp = pmp_to_linux_policy(policy);
    struct sched_param spm;
    memset(&spm, 0, sizeof(spm));
    spm.sched_priority = (int)priority;
    if (sched_setscheduler((pid_t)pid, lp, &spm) < 0)
        return -errno;
    if (nice >= -20 && nice <= 19)
        setpriority(PRIO_PROCESS, (pid_t)pid, nice);
    return 0;
#endif
}

int pmp_sched_get(int32_t pid, pmp_sched_policy_t *policy,
                  int32_t *priority, int *nice) {
#if defined(_WIN32)
    (void)pid; (void)policy; (void)priority; (void)nice;
    return -5;
#else
    struct sched_param spm;
    int lp = sched_getscheduler((pid_t)pid);
    if (lp < 0) return -errno;
    if (policy) *policy = linux_to_pmp_policy(lp);
    if (!sched_getparam((pid_t)pid, &spm) && priority)
        *priority = spm.sched_priority;
    errno = 0;
    int n = getpriority(PRIO_PROCESS, (pid_t)pid);
    if (n != -1 || errno == 0)
        if (nice) *nice = n;
    return 0;
#endif
}

/* ============================================================
 * CPU 亲和性
 * ============================================================ */
int pmp_set_affinity(int32_t pid, uint32_t cpu_mask) {
#if defined(_WIN32)
    (void)pid; (void)cpu_mask;
    return -ENOTSUP;
#else
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int cpu = 0; cpu < (int)(sizeof(cpu_mask) * 8); cpu++)
        if (cpu_mask & (1u << (unsigned)cpu))
            CPU_SET(cpu, &set);
    if (sched_setaffinity((pid_t)pid, sizeof(set), &set) < 0)
        return -errno;
    return 0;
#endif
}

int pmp_get_affinity(int32_t pid, uint32_t *cpu_mask) {
#if defined(_WIN32)
    (void)pid; (void)cpu_mask;
    return -ENOTSUP;
#else
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity((pid_t)pid, sizeof(set), &set) < 0)
        return -errno;
    uint32_t mask = 0;
    for (int cpu = 0; cpu < (int)(sizeof(mask) * 8); cpu++)
        if (CPU_ISSET(cpu, &set))
            mask |= (1u << (unsigned)cpu);
    if (cpu_mask) *cpu_mask = mask;
    return 0;
#endif
}