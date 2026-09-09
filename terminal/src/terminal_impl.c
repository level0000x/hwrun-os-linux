/*
 * terminal_impl.c — TERMINAL 协议插件实现（纯 POSIX PTY）
 *
 * 基于 Linux 伪终端（PTY）子系统，posix_openpt/grantpt/unlockpt/ptsname
 * 分配 master/slave，fork+setsid+ioctl(TIOCSCTTY)+dup2 把 slave 接到子进程
 * 的 stdin/stdout/stderr；master fd 存入会话句柄供 write/read/resize 使用。
 * 用户态、零内核依赖（对内核仅用 devpts 机制）。
 *
 * 会话线程模型：spawn 返回自包含句柄，close 后置 CLOSED 且 master_fd=-1，
 * 再次 write/read/resize 返回 -EBADF。注册表仅用于 list_sessions/status。
 *
 * 错误约定：成功返回 0（write 返回写入字节数）；失败返回负 errno。
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "hwrun.h"
#include "hwrun_plugin.h"
#include "terminal.h"

#ifdef __cplusplus
extern "C" {
#endif

extern char **environ;

/* ============================================================
 * 内部会话注册表（单链，供 list_sessions/status）
 * ============================================================ */
typedef struct hw_term_node {
    hw_terminal_session_t sess;
    struct hw_term_node *next;
} hw_term_node_t;

static hw_term_node_t *g_sessions = NULL;
static unsigned long g_seq = 0;
#define TERM_DEFAULT_COLS 80
#define TERM_DEFAULT_ROWS 24
#define TERM_READ_TIMEOUT_MS 5000

static void set_winsize(int fd, uint32_t cols, uint32_t rows) {
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    ioctl(fd, TIOCSWINSZ, &ws);
}

static hw_term_node_t *find_node(const char *id) {
    if (!id) return NULL;
    for (hw_term_node_t *n = g_sessions; n; n = n->next)
        if (strcmp(n->sess.id, id) == 0) return n;
    return NULL;
}

static void generate_id(char *out, size_t cap) {
    snprintf(out, cap, "term-%06lu", ++g_seq);
}

/* ============================================================
 * spawn：posix_openpt + fork/setsid/slave 接管 + exec
 * ============================================================ */
static int term_spawn(const hw_terminal_spawn_t *cfg, hw_terminal_session_t *out) {
    if (!cfg || !out) return -EINVAL;

    const uint32_t cols = cfg->cols ? cfg->cols : TERM_DEFAULT_COLS;
    const uint32_t rows = cfg->rows ? cfg->rows : TERM_DEFAULT_ROWS;

    const char *cmd;
    char *const *argv = cfg->argv;
    char *const *envp = cfg->envp;
    if (argv) {
        cmd = argv[0];
    } else {
        cmd = cfg->cmd ? cfg->cmd : "/bin/sh";
    }
    if (!cmd || !*cmd) return -EINVAL;

    /* 1. 打开 PTY master */
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) return -errno;
    if (grantpt(master) < 0) {
        int e = -errno;
        close(master);
        return e;
    }
    if (unlockpt(master) < 0) {
        int e = -errno;
        close(master);
        return e;
    }
    char *sp = ptsname(master);
    if (!sp) {
        int e = -errno;
        close(master);
        return e;
    }

    set_winsize(master, cols, rows);
    /* master 非阻塞，配合 read 内 poll 实现有界等待 */
    int fl = fcntl(master, F_GETFL);
    fcntl(master, F_SETFL, fl | O_NONBLOCK);

    pid_t pid = fork();
    if (pid < 0) {
        int e = -errno;
        close(master);
        return e;
    }
    if (pid == 0) {
        /* ---- 子进程：接管 slave 作为控制终端 ---- */
        if (setsid() < 0) _exit(127);
        int slave = open(sp, O_RDWR | O_NOCTTY);
        if (slave < 0) _exit(127);
        if (ioctl(slave, TIOCSCTTY, 0) < 0) {
            close(slave);
            _exit(127);
        }
        set_winsize(slave, cols, rows);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO) close(slave);
        close(master);

        if (!getenv("TERM")) setenv("TERM", "linux", 0);
        if (argv) {
            execvpe(cmd, argv, envp ? envp : environ);
        } else {
            char *def_argv[] = {(char *)cmd, NULL};
            execvpe(cmd, def_argv, envp ? envp : environ);
        }
        _exit(127);
    }

    /* ---- 父进程：登记会话 ---- */
    hw_term_node_t *node = (hw_term_node_t *)calloc(1, sizeof(*node));
    if (!node) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(master);
        return -ENOMEM;
    }
    generate_id(node->sess.id, sizeof(node->sess.id));
    node->sess.cols = cols;
    node->sess.rows = rows;
    node->sess.master_fd = master;
    node->sess.pid = (int)pid;
    node->sess.state = HW_TERMINAL_STATE_ACTIVE;
    snprintf(node->sess.slave_path, sizeof(node->sess.slave_path), "%s", sp);

    node->next = g_sessions;
    g_sessions = node;

    *out = node->sess;
    return 0;
}

/* ============================================================
 * write：往 master 写入输入
 * ============================================================ */
static int term_write(hw_terminal_session_t *s, const uint8_t *buf, uint32_t len) {
    if (!s || (!buf && len)) return -EINVAL;
    if (s->state != HW_TERMINAL_STATE_ACTIVE || s->master_fd < 0) return -EBADF;
    ssize_t n = write(s->master_fd, buf, len);
    if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? (int)0 : -errno;
    return (int)n;
}

/* ============================================================
 * read：poll 等待数据后读取；0=EOF/超时
 * ============================================================ */
static int term_read(hw_terminal_session_t *s, uint8_t *buf, uint32_t cap, uint32_t *out_n) {
    if (!s || !buf || !out_n || cap == 0) return -EINVAL;
    if (s->state != HW_TERMINAL_STATE_ACTIVE || s->master_fd < 0) return -EBADF;
    *out_n = 0;

    struct pollfd pfd;
    pfd.fd = s->master_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int prc = poll(&pfd, 1, TERM_READ_TIMEOUT_MS);
    if (prc < 0) return -errno;
    if (prc == 0) return 0; /* 超时无数据 */
    if (pfd.revents & POLLNVAL) return -EBADF;
    if (!(pfd.revents & (POLLIN | POLLHUP))) return 0;

    ssize_t rd = read(s->master_fd, buf, (size_t)cap);
    if (rd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -errno;
    }
    *out_n = (uint32_t)rd; /* 0=EOF */
    return 0;
}

/* ============================================================
 * resize：更新窗口大小并通知子进程
 * ============================================================ */
static int term_resize(hw_terminal_session_t *s, uint32_t cols, uint32_t rows) {
    if (!s) return -EINVAL;
    if (s->state != HW_TERMINAL_STATE_ACTIVE || s->master_fd < 0) return -EBADF;
    if (cols == 0 || rows == 0) return -EINVAL;
    set_winsize(s->master_fd, cols, rows);
    if (s->pid > 0) kill(s->pid, SIGWINCH);

    s->cols = cols;
    s->rows = rows;
    hw_term_node_t *node = find_node(s->id);
    if (node) {
        node->sess.cols = cols;
        node->sess.rows = rows;
    }
    return 0;
}

/* ============================================================
 * close：终止子进程、关闭 master、移出注册表
 * ============================================================ */
static int term_close(hw_terminal_session_t *s) {
    if (!s) return -EINVAL;

    if (s->pid > 0) {
        kill(s->pid, SIGHUP);
        kill(s->pid, SIGTERM);
        waitpid(s->pid, NULL, WNOHANG);
    }
    if (s->master_fd >= 0) {
        close(s->master_fd);
        s->master_fd = -1;
    }

    hw_term_node_t **pp = &g_sessions;
    while (*pp) {
        if (strcmp((*pp)->sess.id, s->id) == 0) {
            hw_term_node_t *dead = *pp;
            *pp = dead->next;
            free(dead);
            break;
        }
        pp = &(*pp)->next;
    }

    s->state = HW_TERMINAL_STATE_CLOSED;
    return 0;
}

/* ============================================================
 * list_sessions：列出当前活跃会话
 * ============================================================ */
static int term_list_sessions(hw_terminal_session_t *out, int cap, int *count) {
    if (!count) return -EINVAL;
    int total = 0;
    for (hw_term_node_t *n = g_sessions; n; n = n->next) {
        if (out && total < cap) out[total] = n->sess;
        total++;
    }
    *count = total;
    return 0;
}

/* ============================================================
 * status：会话状态 + 子进程存活判定
 * ============================================================ */
static int term_status(const hw_terminal_session_t *s, hw_terminal_status_t *st) {
    if (!s || !st) return -EINVAL;
    hw_term_node_t *node = find_node(s->id);
    if (!node) return -ENOENT;

    st->state = node->sess.state;
    st->cols = node->sess.cols;
    st->rows = node->sess.rows;
    st->pid = node->sess.pid;
    st->running = 0;
    if (node->sess.pid > 0) {
        int wstat = 0;
        pid_t r = waitpid(node->sess.pid, &wstat, WNOHANG);
        st->running = (r == 0);
    }
    return 0;
}

/* ============================================================
 * ops 表与生命周期
 * ============================================================ */
static int32_t term_version(void) {
    return 1;
}

static hw_terminal_ops_t hw_terminal_ops = {
    .version = term_version,
    .spawn = term_spawn,
    .write = term_write,
    .read = term_read,
    .resize = term_resize,
    .close = term_close,
    .list_sessions = term_list_sessions,
    .status = term_status,
};

static int terminal_plugin_init(hw_plugin_t *self) {
    (void)self;
    HWAPI_LOGI("terminal", "init: pty backend (posix_openpt/tcgetattr/TIOCSWINSZ)");
    return HWRUN_OK;
}

static int terminal_plugin_start(hw_plugin_t *self) {
    (void)self;
    return HWRUN_OK;
}

static int terminal_plugin_stop(hw_plugin_t *self) {
    (void)self;
    return HWRUN_OK;
}

static int terminal_plugin_destroy(hw_plugin_t *self) {
    (void)self;
    return HWRUN_OK;
}

static int terminal_plugin_configure(hw_plugin_t *self, const char *key, const char *value) {
    (void)self;
    (void)key;
    (void)value;
    return HWRUN_OK;
}

static void *terminal_plugin_get_interface(const char *protocol) {
    if (!protocol) return NULL;
    if (strcmp(protocol, HWPROTO_TERMINAL) == 0) return &hw_terminal_ops;
    return NULL;
}

static hw_plugin_ops_t g_ops = {
    .init = terminal_plugin_init,
    .start = terminal_plugin_start,
    .stop = terminal_plugin_stop,
    .destroy = terminal_plugin_destroy,
    .configure = terminal_plugin_configure,
    .get_interface = terminal_plugin_get_interface,
};

/* 协议清单：以 NULL 哨兵结尾的只读数组（.so 静态数据，与 plugin.yml 一致） */
static const char *const g_provides[] = {"TERMINAL", NULL};
static const char *const g_requires[] = {"LOG", "PARAM", "METAPROTO", NULL};

HWRUN_PLUGIN_BIND()
HWRUN_PLUGIN_DEFINE("terminal", "Terminal Protocol", "1.0.0", HWPLUGIN_TYPE_TOOLS,
                    "HWRun OS TERMINAL protocol: PTY session mgmt, terminal control, io & resize",
                    &g_ops, g_provides, g_requires)

#ifdef __cplusplus
}
#endif