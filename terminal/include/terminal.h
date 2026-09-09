/*
 * terminal.h — HWRun OS TERMINAL 协议插件公共接口
 *
 * TERMINAL (Terminal Protocol) 是终端协议，提供伪终端（PTY）会话管理、终端
 * 控制、行规程处理等终端功能的统一抽象。面向 SHELL / CONSOLE / UI / DESKTOP
 * / SSH 等上层插件。
 * 依据根目录设计稿《HWRun OS TERMIN.txt》（状态：设计冻结）落地。
 *
 * 依赖链：METAPROTO → BUS → PARAM → LOG → TERMINAL
 * requires: LOG / PARAM / METAPROTO
 *
 * 实现方案：真实使用 POSIX PTY——posix_openpt/grantpt/unlockpt/ptsname +
 * termios + ioctl(TIOCSWINSZ)，用户态、零内核依赖（对 Linux 内核仅用到 devpts
 * 机制，与普通开发机一致）。绝无假数据/占位。
 *
 * 会话模型：spawn 返回一个自包含的 hw_terminal_session_t 句柄，调用方持有
 * master_fd；后续 write/read/resize/close/status 直接以该句柄操作，无需二次
 * 查找。close 后句柄置 CLOSED 且 master_fd=-1，再次读写返回 -EBADF。
 *
 * 错误约定：成功返回 HWRUN_OK(0)（write 返回实际写入字节数）；失败返回负
 * errno（-EINVAL/-ENOENT/-EBADF/-ENOMEM/...），可用 strerror(-rc) 转描述。
 */

#ifndef HWRUN_TERMINAL_H
#define HWRUN_TERMINAL_H

#include "hwrun.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 会话状态（设计稿 terminal_state_t）
 * ============================================================ */
typedef enum {
    HW_TERMINAL_STATE_CREATED = 0, /* 已创建 */
    HW_TERMINAL_STATE_ACTIVE,      /* 活跃（子进程运行中，可读写） */
    HW_TERMINAL_STATE_CLOSED,      /* 已关闭（写读返回 -EBADF） */
    HW_TERMINAL_STATE_ERROR,       /* 错误状态 */
} hw_terminal_state_t;

/* ============================================================
 * PTY 会话句柄（spawn 时由实现填充，调用方按值持有）
 * ============================================================ */
typedef struct hw_terminal_session {
    char id[64];   /* 会话 ID（形如 term-<seq>） */
    uint32_t cols; /* 列数 */
    uint32_t rows; /* 行数 */
    int master_fd; /* PTY master 描述符（写/读/缩放用） */
    int pid;       /* 子进程 PID（无则 -1） */
    hw_terminal_state_t state;
    char slave_path[64]; /* /dev/pts/N */
} hw_terminal_session_t;

/* ============================================================
 * spawn 配置
 * ============================================================ */
typedef struct hw_terminal_spawn {
    const char *cmd;   /* 可执行路径；argv 为 NULL 时作为程序名，再缺省 /bin/sh */
    char *const *argv; /* NULL 结尾 argv；NULL 则自动构造 {cmd, NULL} */
    char *const *envp; /* NULL 结尾环境；NULL 则继承 environ */
    uint32_t cols;     /* 默认 80 */
    uint32_t rows;     /* 默认 24 */
} hw_terminal_spawn_t;

/* ============================================================
 * 会话状态快照（status 填充）
 * ============================================================ */
typedef struct hw_terminal_status {
    hw_terminal_state_t state;
    uint32_t cols;
    uint32_t rows;
    int pid;
    int running; /* 子进程是否存活（waitpid WNOHANG 判定） */
} hw_terminal_status_t;

/* ============================================================
 * TERMINAL 协议接口 (get_interface("TERMINAL") 返回此指针)
 * ============================================================ */
typedef struct hw_terminal_ops {
    int32_t (*version)(void);

    /* 创建 PTY 会话并运行命令；out 填充分配到的会话句柄 */
    int (*spawn)(const hw_terminal_spawn_t *cfg, hw_terminal_session_t *out);

    /* 往 pty 写 len 字节，返回实际写入字节数（>=0）或负 errno */
    int (*write)(hw_terminal_session_t *s, const uint8_t *buf, uint32_t len);

    /* 读最多 cap 字节到 buf；成功 out_n 置实际字节数（0=EOF/超时） */
    int (*read)(hw_terminal_session_t *s, uint8_t *buf, uint32_t cap, uint32_t *out_n);

    /* 调整窗口大小（发送 SIGWINCH 通知子进程） */
    int (*resize)(hw_terminal_session_t *s, uint32_t cols, uint32_t rows);

    /* 关闭会话：终止子进程、关闭 master、置 CLOSED。返回 0 或负 errno */
    int (*close)(hw_terminal_session_t *s);

    /* 列出当前活跃会话到 out[0..cap)；count 置注册表数量 */
    int (*list_sessions)(hw_terminal_session_t *out, int cap, int *count);

    /* 查询会话状态（含子进程存活判定） */
    int (*status)(const hw_terminal_session_t *s, hw_terminal_status_t *st);
} hw_terminal_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_TERMINAL_H */