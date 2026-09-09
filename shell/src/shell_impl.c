/*
 * shell_impl.c — SHELL 协议插件完整实现（用户态命令行解释器）
 *
 * 实现 hw_plugin_entry()：返回 hw_plugin_t*，其 ops 提供
 * init/start/stop/destroy/configure/get_interface。get_interface("SHELL")
 * 返回 hw_shell_ops_t*（真实实现，零内核依赖）。
 *
 * 内置命令：help / echo / pwd / history / alias / exit / quit。
 * 非内置行：fork() + exec /bin/sh -c <line>，经 pipe 捕获 stdout+stderr。
 * 历史：进程内环形数组；别名：进程内名称->值表，eval 对命令首词单层展开。
 *
 * 错误约定：成功返回 HWRUN_OK(0)；失败返回负 errno。
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>

#include "hwrun.h"
#include "hwrun_plugin.h"
#include "shell.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 常量
 * ============================================================ */
#define SHELL_LINE_MAX 255    /* 单行命令最大长度（历史条目同宽） */
#define SHELL_ARGS_MAX 64     /* 单行最多拆分参数数 */
#define SHELL_HISTORY_MAX 100 /* 历史环形数组容量 */
#define SHELL_ALIAS_MAX 64    /* 别名表容量 */

/* ============================================================
 * 状态（进程内）
 * ============================================================ */
typedef struct shell_builtin {
    const char *name;
    int (*func)(int argc, char **argv, char *out, size_t cap, size_t *n);
} shell_builtin_t;

static char g_history[SHELL_HISTORY_MAX][SHELL_LINE_MAX]; /* 环形历史，按插入序从旧到新 */
static int g_history_count = 0;                           /* 已存在条数 */
static int g_history_write = 0; /* 环形写指针（下一写入槽位） */

static char g_alias_name[SHELL_ALIAS_MAX][64];
static char g_alias_value[SHELL_ALIAS_MAX][256];
static int g_alias_count = 0;

/* ============================================================
 * 输出缓冲辅助
 * ============================================================ */
static void buf_append(char *out, size_t cap, size_t *n, const void *src, size_t len) {
    if (*n + len > cap) len = (cap > *n) ? (cap - *n) : 0;
    if (len) {
        memcpy(out + *n, src, len);
        *n += len;
    }
}

static void buf_append_str(char *out, size_t cap, size_t *n, const char *s) {
    buf_append(out, cap, n, s, strlen(s));
}

static void buf_append_fmt(char *out, size_t cap, size_t *n, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    buf_append_str(out, cap, n, tmp);
}

/* ============================================================
 * 行处理：去首尾空白 / 拆分参数
 * ============================================================ */
static void trim_copy(const char *src, char *dst, size_t cap) {
    if (!src || !dst || cap == 0) return;
    size_t len = strlen(src);
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
    char *b = dst;
    while (*b == ' ' || *b == '\t' || *b == '\n')
        b++;
    char *e = b + strlen(b);
    while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n'))
        e--;
    *e = '\0';
    memmove(dst, b, (size_t)(e - b) + 1);
}

/* 就地拆分：把 buf 的空白分隔词写入 argv（指向 buf 内部），返回词数 */
static int split_tokens(char *buf, char **argv, int max) {
    int argc = 0;
    char *p = buf;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (!*p) break;
        if (argc >= max) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = NULL;
    return argc;
}

/* 把 argv 重新拼成一行（供外部命令交给 /bin/sh -c） */
static void join_line(char **argv, int argc, char *out, size_t cap) {
    size_t pos = 0;
    for (int i = 0; i < argc; i++) {
        size_t need = (i ? 1u : 0u) + strlen(argv[i]);
        if (pos + need >= cap) break;
        if (i) out[pos++] = ' ';
        memcpy(out + pos, argv[i], strlen(argv[i]));
        pos += strlen(argv[i]);
    }
    out[pos] = '\0';
}

/* ============================================================
 * 历史（环形数组，按插入序从旧到新）
 * ============================================================ */
static int history_append(const char *line) {
    if (!line || line[0] == '\0') return -EINVAL;
    snprintf(g_history[g_history_write], SHELL_LINE_MAX, "%s", line);
    g_history_write = (g_history_write + 1) % SHELL_HISTORY_MAX;
    if (g_history_count < SHELL_HISTORY_MAX) g_history_count++;
    return HWRUN_OK;
}

static int history_get(int idx, char *out, size_t cap) {
    if (!out || cap == 0) return -EINVAL;
    if (idx < 0 || idx >= g_history_count) return -ENOENT;
    int slot = (g_history_write - g_history_count + idx) % SHELL_HISTORY_MAX;
    if (slot < 0) slot += SHELL_HISTORY_MAX;
    snprintf(out, cap, "%s", g_history[slot]);
    return HWRUN_OK;
}

static int history_list(char *out, size_t cap, size_t *n) {
    if (!out || cap == 0 || !n) return -EINVAL;
    *n = 0;
    for (int i = 0; i < g_history_count; i++) {
        char line[SHELL_LINE_MAX];
        history_get(i, line, sizeof(line));
        buf_append_fmt(out, cap, n, "%4d  %s\n", i + 1, line);
    }
    return HWRUN_OK;
}

static int history_clear(void) {
    g_history_count = 0;
    g_history_write = 0;
    return HWRUN_OK;
}

static int history_count(void) {
    return g_history_count;
}

/* ============================================================
 * 别名
 * ============================================================ */
static int alias_set(const char *name, const char *value) {
    if (!name || name[0] == '\0' || !value) return -EINVAL;
    if (strchr(name, '=') || strchr(name, ' ')) return -EINVAL;
    for (int i = 0; i < g_alias_count; i++)
        if (strcmp(g_alias_name[i], name) == 0) {
            snprintf(g_alias_value[i], sizeof(g_alias_value[i]), "%s", value);
            return HWRUN_OK;
        }
    if (g_alias_count >= SHELL_ALIAS_MAX) return -ENOSPC;
    snprintf(g_alias_name[g_alias_count], sizeof(g_alias_name[g_alias_count]), "%s", name);
    snprintf(g_alias_value[g_alias_count], sizeof(g_alias_value[g_alias_count]), "%s", value);
    g_alias_count++;
    return HWRUN_OK;
}

static int alias_get(const char *name, char *out, size_t cap) {
    if (!name || !out || cap == 0) return -EINVAL;
    for (int i = 0; i < g_alias_count; i++)
        if (strcmp(g_alias_name[i], name) == 0) {
            snprintf(out, cap, "%s", g_alias_value[i]);
            return HWRUN_OK;
        }
    return -ENOENT;
}

static int alias_clear(void) {
    g_alias_count = 0;
    return HWRUN_OK;
}

static int alias_count(void) {
    return g_alias_count;
}

/* ============================================================
 * 内置命令
 * ============================================================ */
static int cmd_help(int argc, char **argv, char *out, size_t cap, size_t *n) {
    (void)argc;
    (void)argv;
    buf_append_str(out, cap, n,
                   "HWRun OS SHELL builtins: help echo pwd history alias exit quit\n"
                   "  help            show this help\n"
                   "  echo <text>     print arguments joined by spaces\n"
                   "  pwd             print current working directory\n"
                   "  history         list command history\n"
                   "  alias           list / set / show aliases\n"
                   "  exit|quit       leave the shell\n"
                   "  other lines run via /bin/sh -c with stdout+stderr captured\n");
    return HWRUN_OK;
}

static int cmd_echo(int argc, char **argv, char *out, size_t cap, size_t *n) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) buf_append(out, cap, n, " ", 1);
        buf_append_str(out, cap, n, argv[i]);
    }
    buf_append(out, cap, n, "\n", 1);
    return HWRUN_OK;
}

static int cmd_pwd(int argc, char **argv, char *out, size_t cap, size_t *n) {
    (void)argc;
    (void)argv;
    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd))) return -errno;
    buf_append_str(out, cap, n, cwd);
    buf_append(out, cap, n, "\n", 1);
    return HWRUN_OK;
}

static int cmd_history(int argc, char **argv, char *out, size_t cap, size_t *n) {
    (void)argc;
    (void)argv;
    return history_list(out, cap, n);
}

static int cmd_alias(int argc, char **argv, char *out, size_t cap, size_t *n) {
    if (argc == 1) { /* 列出全部 */
        for (int i = 0; i < g_alias_count; i++)
            buf_append_fmt(out, cap, n, "%s='%s'\n", g_alias_name[i], g_alias_value[i]);
        return HWRUN_OK;
    }
    char *arg = argv[1];
    char *eq = strchr(arg, '=');
    if (eq) { /* name=value 设置 */
        *eq = '\0';
        int rc = alias_set(arg, eq + 1);
        *eq = '=';
        return rc;
    }
    char val[256];
    if (alias_get(arg, val, sizeof(val)) != HWRUN_OK) return -ENOENT;
    buf_append_fmt(out, cap, n, "%s='%s'\n", arg, val);
    return HWRUN_OK;
}

static int cmd_exit(int argc, char **argv, char *out, size_t cap, size_t *n) {
    (void)argc;
    (void)argv;
    buf_append_str(out, cap, n, "bye\n");
    return HWRUN_OK;
}

/* 内置命令表 */
static const shell_builtin_t g_builtins[] = {
    {"help", cmd_help},   {"echo", cmd_echo}, {"pwd", cmd_pwd},   {"history", cmd_history},
    {"alias", cmd_alias}, {"exit", cmd_exit}, {"quit", cmd_exit},
};
#define G_BUILTINS_N (sizeof(g_builtins) / sizeof(g_builtins[0]))

static const shell_builtin_t *builtin_find(const char *name) {
    for (size_t i = 0; i < G_BUILTINS_N; i++)
        if (strcmp(g_builtins[i].name, name) == 0) return &g_builtins[i];
    return NULL;
}

static int builtin_count(void) {
    return (int)G_BUILTINS_N;
}

/* ============================================================
 * 外部命令：fork + exec /bin/sh -c <line>，pipe 捕获 stdout+stderr
 * ============================================================ */
static int run_external(const char *line, char *out, size_t cap, size_t *n) {
    int p[2];
    if (pipe(p) != 0) return -errno;
    pid_t pid = fork();
    if (pid < 0) {
        int e = -errno;
        close(p[0]);
        close(p[1]);
        return e;
    }
    if (pid == 0) { /* 子进程 */
        close(p[0]);
        dup2(p[1], STDOUT_FILENO);
        dup2(p[1], STDERR_FILENO);
        close(p[1]);
        execl("/bin/sh", "sh", "-c", line, (char *)NULL);
        _exit(127);
    }
    /* 父进程：读管道，超过 cap 的部分丢弃 */
    close(p[1]);
    size_t total = 0;
    for (;;) {
        if (total < cap) {
            ssize_t rd = read(p[0], out + total, cap - total);
            if (rd > 0) {
                total += (size_t)rd;
                continue;
            }
            if (rd < 0 && errno == EINTR) continue;
            break;
        }
        char tmp[1024];
        ssize_t rd = read(p[0], tmp, sizeof(tmp));
        if (rd <= 0) break;
    }
    close(p[0]);
    int status;
    waitpid(pid, &status, 0);
    *n = total;
    return HWRUN_OK;
}

/* ============================================================
 * 执行一段已经 tokenize 的 argv
 * ============================================================ */
static int exec_tokens(char **argv, int argc, char *out, size_t cap, size_t *n) {
    const shell_builtin_t *cmd = builtin_find(argv[0]);
    if (cmd) return cmd->func(argc, argv, out, cap, n);
    char line[SHELL_LINE_MAX * 4];
    join_line(argv, argc, line, sizeof(line));
    return run_external(line, out, cap, n);
}

/* ============================================================
 * SHELL 协议接口实现
 * ============================================================ */
static int32_t shell_version(void) {
    return 1;
}

/* 解析并执行一行：去空白 -> 入历史 -> 别名展开 -> 内置或外部命令 */
static int ops_eval(const char *line, char *out, size_t cap, size_t *n) {
    if (!line || !out || cap == 0 || !n) return -EINVAL;
    char buf[SHELL_LINE_MAX * 4];
    trim_copy(line, buf, sizeof(buf));
    if (buf[0] == '\0') return -EINVAL; /* 空行 */

    *n = 0;
    int hrc = history_append(buf);
    if (hrc != HWRUN_OK) return hrc;

    char *argv[SHELL_ARGS_MAX];
    int argc = split_tokens(buf, argv, SHELL_ARGS_MAX);
    if (argc == 0) return -EINVAL;

    /* 单层别名展开：命令首词命中别名则前置其值再执行 */
    char val[256];
    if (alias_get(argv[0], val, sizeof(val)) == HWRUN_OK && val[0] != '\0') {
        char exp[SHELL_LINE_MAX * 4];
        size_t pos = 0;
        size_t vlen = strlen(val);
        memcpy(exp, val, vlen);
        pos = vlen;
        for (int i = 1; i < argc; i++) {
            if (pos + strlen(argv[i]) + 2 >= sizeof(exp)) break;
            exp[pos++] = ' ';
            size_t l = strlen(argv[i]);
            memcpy(exp + pos, argv[i], l);
            pos += l;
        }
        exp[pos] = '\0';
        char *exp_argv[SHELL_ARGS_MAX];
        int exp_argc = split_tokens(exp, exp_argv, SHELL_ARGS_MAX);
        if (exp_argc > 0) return exec_tokens(exp_argv, exp_argc, out, cap, n);
    }
    return exec_tokens(argv, argc, out, cap, n);
}

static int ops_run(char *const argv[]) {
    if (!argv || !argv[0]) return -EINVAL;
    for (int i = 0; argv[i]; i++)
        if (argv[i][0] == '\0') return -EINVAL;

    char *av[SHELL_ARGS_MAX];
    int ac = 0;
    for (; argv[ac] && ac < SHELL_ARGS_MAX; ac++)
        av[ac] = argv[ac];

    char val[256];
    if (alias_get(av[0], val, sizeof(val)) == HWRUN_OK && val[0] != '\0') {
        char scratch[SHELL_LINE_MAX * 4];
        snprintf(scratch, sizeof(scratch), "%s", val);
        char *vtok[SHELL_ARGS_MAX];
        int vc = split_tokens(scratch, vtok, SHELL_ARGS_MAX);
        char *next[SHELL_ARGS_MAX];
        int nc = 0;
        for (int i = 0; i < vc && nc < SHELL_ARGS_MAX; i++)
            next[nc++] = vtok[i];
        for (int i = 1; i < ac && nc < SHELL_ARGS_MAX; i++)
            next[nc++] = av[i];
        /* vtok 指向本帧 scratch，exec_tokens 内同步使用，安全 */
        char out[1024];
        size_t n = 0;
        return exec_tokens(next, nc, out, sizeof(out), &n);
    }
    char out[1024];
    size_t n = 0;
    return exec_tokens(av, ac, out, sizeof(out), &n);
}

static int ops_status(char *out, size_t cap, size_t *n) {
    if (!out || cap == 0 || !n) return -EINVAL;
    char cwd[256];
    *n = 0;
    if (!getcwd(cwd, sizeof(cwd))) return -errno;
    buf_append_fmt(out, cap, n, "shell: SHELL1.0 builtins=%d aliases=%d history=%d cwd=%s\n",
                   builtin_count(), g_alias_count, g_history_count, cwd);
    return HWRUN_OK;
}

/* ---- ops 表（供 get_interface("SHELL") 返回） ---- */
static hw_shell_ops_t hw_shell_ops = {
    .version = shell_version,
    .eval = ops_eval,
    .run = ops_run,
    .history_append = history_append,
    .history_get = history_get,
    .history_list = history_list,
    .history_clear = history_clear,
    .history_count = history_count,
    .alias_set = alias_set,
    .alias_get = alias_get,
    .alias_clear = alias_clear,
    .alias_count = alias_count,
    .status = ops_status,
};

/* ============================================================
 * 插件生命周期
 * ============================================================ */
static int g_plugin_state = HWPLUGIN_LOADED;

static int shell_plugin_init(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_LOADED;
    HWAPI_LOGI("shell", "init: SHELL1.0 builtins=%d", builtin_count());
    return HWRUN_OK;
}

static int shell_plugin_start(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_STARTED;
    return HWRUN_OK;
}

static int shell_plugin_stop(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_STOPPED;
    return HWRUN_OK;
}

static int shell_plugin_destroy(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_UNINSTALLED;
    return HWRUN_OK;
}

static int shell_plugin_configure(hw_plugin_t *self, const char *key, const char *value) {
    (void)self;
    HWAPI_LOGI("shell", "configure: %s=%s", key ? key : "(null)", value ? value : "(null)");
    return HWRUN_OK;
}

static void *shell_plugin_get_interface(const char *protocol) {
    if (!protocol) return NULL;
    if (strcmp(protocol, HWPROTO_SHELL) == 0) return &hw_shell_ops;
    return NULL;
}

static hw_plugin_ops_t g_ops = {
    .init = shell_plugin_init,
    .start = shell_plugin_start,
    .stop = shell_plugin_stop,
    .destroy = shell_plugin_destroy,
    .configure = shell_plugin_configure,
    .get_interface = shell_plugin_get_interface,
};

static const char *const g_provides[] = {"SHELL", NULL};
static const char *const g_requires[] = {"LOG", "PARAM", "METAPROTO", NULL};

HWRUN_PLUGIN_BIND()
HWRUN_PLUGIN_DEFINE("shell", "Shell Interpreter", "1.0.0", HWPLUGIN_TYPE_TOOLS,
                    "HWRun OS SHELL protocol: command-line interpreter with builtins, history, "
                    "alias, external exec",
                    &g_ops, g_provides, g_requires)

#ifdef __cplusplus
}
#endif