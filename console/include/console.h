/*
 * console.h — HWRun OS CONSOLE 协议插件公共接口
 *
 * CONSOLE (System Console) 是系统控制台协议，提供基于读终端/标准 I/O 的
 * 简单控制台抽象：会话管理、banner、逐行/原始 I/O、状态上报、可挂接终端
 * 输出。零内核依赖，用户态实现。依据根目录设计稿《HWRun OS 上层应用插件.txt》
 * （五、CONSOLE 实现文档，状态：设计冻结）落地并精简为可测的抽象。
 *
 * 依赖链：METAPROTO → BUS → PARAM → LOG → CONSOLE
 * requires: LOG / PARAM / METAPROTO
 *
 * 会话模型：
 *   - open(name) 新建会话，同名活动会话返回 -EEXIST。
 *   - 会话内部用可增长的文本行缓冲承载 write/read_line；脱离真终端亦可测。
 *   - write 原样追加；read_line 逐行弹出（遇 '\n' 完行）；无完整行返回 -EAGAIN。
 *   - banner 把文本中的 '\n' 转义为字面 "\n" 并置为行缓冲首行。
 *   - 会话可绑定一个输出回调（可选），write/banner 时回调挂接终端。
 *
 * 错误约定：成功返回 HWRUN_OK(0)；失败返回负 errno（-EINVAL/-EEXIST/-ENOMEM/
 * -ENOBUFS/-EAGAIN）。文件描述符语义按零内核的读终端抽象建模。
 */

#ifndef HWRUN_CONSOLE_H
#define HWRUN_CONSOLE_H

#include "hwrun.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HW_CONSOLE_NAME_MAX 32 /* 会话名称上限，如 "tty0"、"console" */

/* 会话句柄：指向实现内部会话，为不透明类型 */
typedef struct hw_console_session hw_console_session_t;

/* ============================================================
 * 控制台/会话状态（status / list_sessions 输出）
 * ============================================================ */
typedef struct hw_console_info {
    char name[HW_CONSOLE_NAME_MAX]; /* 会话名称 */
    uint64_t id;                    /* 自增会话 id */
    int active;                     /* 1=活动；0=已关闭（仅 status 可能为 0） */
    uint64_t lines;                 /* 当前可读完整行数（含 '\n' 计） */
    uint64_t written;               /* 累计写入字节 */
    size_t buffered;                /* 行缓冲当前占用字节 */
} hw_console_info_t;

/* ============================================================
 * CONSOLE 协议接口 (get_interface("CONSOLE") 返回此指针)
 * ============================================================ */
typedef struct hw_console_ops {
    int32_t (*version)(void);

    /* 会话生命周期 */
    int (*open)(const char *name, hw_console_session_t **session);
    int (*close)(hw_console_session_t *session);

    /* 原始 I/O：write 原样追加；read_line 逐行弹出 */
    int (*write)(hw_console_session_t *session, const char *buf, size_t len);
    int (*read_line)(hw_console_session_t *session, char *buf, size_t cap, size_t *n);

    /* banner：'\n' 转义并置为行缓冲首行 */
    int (*banner)(hw_console_session_t *session, const char *text);

    /* clear：清空行缓冲 */
    int (*clear)(hw_console_session_t *session);

    /* 状态上报 / 会话枚举 */
    int (*status)(hw_console_session_t *session, hw_console_info_t *out);
    int (*list_sessions)(hw_console_info_t *out, int cap, int *count);

    /* 绑定输出回调（可选）：write/banner 时回调；cb=NULL 解绑 */
    int (*bind_output)(hw_console_session_t *session,
                       void (*cb)(const void *ctx, const char *data, size_t len), const void *ctx);
} hw_console_ops_t;

/* 插件内部：释放全部会话（stop/destroy 时调用）。仅供插件实现使用。 */
void hw_console_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_CONSOLE_H */