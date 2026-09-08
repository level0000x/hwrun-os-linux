/*
 * pmp.h — HWRun OS PMP（进程管理协议）公共接口
 *
 * 依赖链第 4 环（HAP -> PMP）。本插件以 .so 形式存在，
 * 通过 hw_plugin_entry() 导出，get_interface("PMP") 返回 hw_pmp_ops_t*。
 *
 * 实现使用 Linux/POSIX 系统调用（fork/execve/waitpid/kill/sched_*）。
 * 在 MSYS2 测试环境不具备 fork 等能力时优雅降级（返回非零错误码，不崩溃）。
 */

#ifndef HWRUN_PMP_H
#define HWRUN_PMP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HWPROTO_PMP "PMP"

/* ============================================================
 * 进程状态（对应 Linux task state 的字母映射）
 * ============================================================ */
typedef enum {
    PMP_TASK_RUNNING = 0,         /* R 可运行    */
    PMP_TASK_INTERRUPTIBLE = 1,   /* S 可中断睡眠 */
    PMP_TASK_UNINTERRUPTIBLE = 2, /* D 不可中断睡眠*/
    PMP_TASK_ZOMBIE = 3,          /* Z 僵尸进程   */
    PMP_TASK_STOPPED = 4,         /* T 已停止    */
    PMP_TASK_TRACING = 5,         /* t 被跟踪    */
    PMP_TASK_DEAD = 6,            /* X 已死亡    */
    PMP_TASK_WAKEKILL = 7,        /* K, W 等    */
    PMP_TASK_UNKNOWN = 8,         /* 未知/无法识别 */
} pmp_task_state_t;

/* ============================================================
 * 调度策略（映射到 Linux sched policy）
 * ============================================================ */
typedef enum {
    PMP_SCHED_OTHER = 0,    /* SCHED_OTHER 普通分时   */
    PMP_SCHED_FIFO = 1,     /* SCHED_FIFO  实时 FIFO */
    PMP_SCHED_RR = 2,       /* SCHED_RR    实时轮转  */
    PMP_SCHED_BATCH = 3,    /* SCHED_BATCH  批处理   */
    PMP_SCHED_IDLE = 4,     /* SCHED_IDLE   空闲调度 */
    PMP_SCHED_DEADLINE = 5, /* SCHED_DEADLINE 截止时间 */
} pmp_sched_policy_t;

/* ============================================================
 * 进程信息（单条）
 * ============================================================ */
typedef struct pmp_process_info {
    uint32_t pid;              /* 进程 ID          */
    uint32_t ppid;             /* 父进程 ID        */
    uint32_t tgid;             /* 线程组 ID        */
    char comm[64];             /* 进程名称         */
    pmp_task_state_t state;    /* 进程状态         */
    int32_t priority;          /* 优先级(/proc 静态优先级) */
    int nice;                  /* Nice 值(-20~19)  */
    pmp_sched_policy_t policy; /* 调度策略         */
    uint64_t cpu_time_user;    /* 用户态 CPU 时间(clock ticks) */
    uint64_t cpu_time_system;  /* 内核态 CPU 时间(clock ticks) */
    uint32_t cpu_affinity;     /* CPU 亲和性掩码(最低 CPU) */
    uint64_t memory_rss;       /* RSS 物理内存(KB) */
    uint64_t memory_virtual;   /* 虚拟内存(KB)    */
    uint64_t start_time;       /* 启动时间(相对系统启动的 ticks) */
} pmp_process_info_t;

/* ============================================================
 * 任务状态（task 管理：submit/cancel）
 * ============================================================ */
typedef enum {
    PMP_TASK_STATUS_PENDING = 0,   /* 已提交，未开始  */
    PMP_TASK_STATUS_RUNNING = 1,   /* 运行中         */
    PMP_TASK_STATUS_FINISHED = 2,  /* 正常结束       */
    PMP_TASK_STATUS_FAILED = 3,    /* 失败           */
    PMP_TASK_STATUS_CANCELLED = 4, /* 已取消         */
    PMP_TASK_STATUS_UNKNOWN = 5,   /* 未知任务 ID     */
} pmp_task_status_t;

typedef struct pmp_task_info {
    uint64_t id;             /* 任务 ID        */
    char name[64];           /* 任务名称       */
    char path[256];          /* 可执行文件路径 */
    int32_t pid;             /* 对应操作系统 PID(-1=尚未生成) */
    pmp_task_status_t state; /* 任务状态       */
    int exit_status;         /* 退出状态       */
} pmp_task_info_t;

/* ============================================================
 * PMP 协议接口（hw_plugin_ops.get_interface("PMP") 返回）
 *
 * 全部返回：0 = 成功(HWRUN_OK)，负数 = 出错（见 hwrun.h 错误码）。
 * 进程级函数在无法执行对应 POSIX 能力时（缺失/权限/Windows 无 fork）
 * 优雅返回非零错误码，绝不崩溃。
 * ============================================================ */
typedef struct hw_pmp_ops {
    /* ---------- 进程查询 ---------- */
    int (*get_process_list)(pmp_process_info_t **out_list, uint32_t *out_count);
    void (*free_process_list)(pmp_process_info_t *list, uint32_t count);
    int (*get_process_info)(int32_t pid, pmp_process_info_t *out_info);
    int (*get_current_pid)(void);

    /* ---------- 进程创建 ---------- */
    /* fork：返回子进程 pid（父进程侧）；子进程返回 0；失败 < 0 */
    int (*fork)(void);
    /* exec：用给定 argv 在 *当前进程* 执行 path；成功不返回，失败返回错误码 */
    int (*exec)(const char *path, char *const argv[]);
    /* execve_full：带 envp 的完整执行 */
    int (*execve_full)(const char *path, char *const argv[], char *const envp[]);
    /* spawn：fork+exec 封装，创建工作子进程并返回其 pid（out_pid>=0 成功） */
    int (*spawn)(const char *path, char *const argv[], int *out_pid);

    /* ---------- 进程控制 ---------- */
    int (*wait)(int32_t pid, int *status, int options); /* waitpid 封装 */
    int (*signal)(int32_t pid, int sig);                /* kill 发送信号 */
    int (*kill)(int32_t pid, int sig);                  /* signal 别名  */

    /* 进程启停：start=交由调度(SIGCONT)，stop=SIGSTOP，pause=同 stop，
       resume=SIGCONT */
    int (*start)(int32_t pid);
    int (*stop)(int32_t pid);
    int (*pause_process)(int32_t pid);
    int (*resume)(int32_t pid);

    /* ---------- 调度 ---------- */
    int (*sched_set)(int32_t pid, pmp_sched_policy_t policy, int32_t priority, int32_t nice);
    int (*sched_get)(int32_t pid, pmp_sched_policy_t *policy, int32_t *priority, int *nice);

    /* ---------- CPU 亲和性 ---------- */
    int (*set_affinity)(int32_t pid, uint32_t cpu_mask);
    int (*get_affinity)(int32_t pid, uint32_t *cpu_mask);

    /* ---------- 任务管理（task 提交/取消） ---------- */
    /* 提交任务：后台 fork 运行 path，返回 task id（>=0）。失败 out_err 为错误码。 */
    uint64_t (*task_submit)(const char *name, const char *path, char *const argv[], int *out_err);
    int (*task_cancel)(uint64_t task_id);
    int (*task_status)(uint64_t task_id, pmp_task_info_t *out);
} hw_pmp_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_PMP_H */