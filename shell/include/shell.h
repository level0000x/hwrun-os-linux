/*
 * shell.h — HWRun OS SHELL 协议插件公共接口
 *
 * SHELL (Shell Protocol) 是命令行解释器协议，接收用户命令并执行。
 * 依据根目录设计稿《HWRun OS 上层应用插件.txt》（SHELL 设计要点，状态：设计冻结）落地。
 *
 * 依赖链：METAPROTO → BUS → PARAM → LOG → SHELL
 * requires: LOG / PARAM / METAPROTO
 *
 * 实现方案（用户态、零内核依赖）：
 *   - 内置命令：help / echo / pwd / history / alias / exit / quit 等；
 *   - 历史记录：进程内环形数组，eval 自动追加；
 *   - 别名：进程内名称->值表，eval 时对命令首词做一层展开；
 *   - 外部命令：fork() 后 exec /bin/sh -c <line>，通过 pipe 捕获 stdout+stderr。
 *
 * 错误约定：成功返回 HWRUN_OK(0)；失败返回负 errno（-EINVAL/-ENOENT/-ENOMEM/...）。
 * 捕获输出写入调用方提供的 out[cap]，实际字节数经 *n 返回；超出 cap 部分截断。
 */

#ifndef HWRUN_SHELL_H
#define HWRUN_SHELL_H

#include "hwrun.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * SHELL 协议接口 (get_interface("SHELL") 返回此指针)
 * ============================================================ */
typedef struct hw_shell_ops {
    int32_t (*version)(void);

    /* 解析并执行一行命令，捕获 stdout/stderr 到 out[cap)，实际字节写入 *n。
     * 空行 / 全空白 / NULL / 非法参数返回负 errno。执行成功返回 HWRUN_OK。
     * eval 会把该行自动追加到历史。 */
    int (*eval)(const char *line, char *out, size_t cap, size_t *n);

    /* 直接执行 argv 形式的命令（不入历史），成功返回 0。argv 以 NULL 结尾。 */
    int (*run)(char *const argv[]);

    /* ---- 历史（进程内环形数组） ---- */
    int (*history_append)(const char *line);
    int (*history_get)(int idx, char *out, size_t cap); /* 0-based 插入序 */
    int (*history_list)(char *out, size_t cap, size_t *n);
    int (*history_clear)(void);
    int (*history_count)(void);

    /* ---- 别名 ---- */
    int (*alias_set)(const char *name, const char *value);
    int (*alias_get)(const char *name, char *out, size_t cap);
    int (*alias_clear)(void);
    int (*alias_count)(void);

    /* 状态：把 builtin 数 / 别名数 / 历史数 / cwd 摘要写入 out */
    int (*status)(char *out, size_t cap, size_t *n);
} hw_shell_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_SHELL_H */