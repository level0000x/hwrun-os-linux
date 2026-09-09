/*
 * console_impl.c — CONSOLE 协议实现（内存行缓冲的读终端抽象）
 *
 * 零内核依赖：会话内部用可增长的文本行缓冲承载 write/read_line，把 Linux
 * /dev/console 的读终端接口建模为内存抽象，脱离真终端亦可完整测试。
 *
 *   - open(name)：新建会话；同名活动会话返回 -EEXIST。
 *   - write：原样追加到行缓冲，统计 written；绑定了输出回调则逐次回调。
 *   - read_line：弹出首个完整行（遇 '\n' 完行，不含换行符）；无完整行返回
 *     -EAGAIN；行过长放不进取返回 -ENOBUFS（不消费）。
 *   - banner：把文本中的 '\n' 转义为字面 "\n"，并作为行缓冲首行。
 *   - clear：清空行缓冲。close：标记关闭并从活动集合移除；关闭后读写返回
 *     -EINVAL。已关闭会话保持已分配内存，统一由 hw_console_cleanup() 释放。
 *
 * 错误约定：成功返回 0；失败返回负 errno。
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "console.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 内部会话对象 */
typedef struct hw_console_session {
    char name[HW_CONSOLE_NAME_MAX];
    uint64_t id;
    int active;       /* 1=活动；0=已关闭 */
    char *buf;        /* 行缓冲 */
    size_t len;       /* 已用字节 */
    size_t cap;       /* 已分配字节 */
    uint64_t written; /* 累计写入字节 */
    void (*out_cb)(const void *ctx, const char *data, size_t len);
    const void *out_ctx;
    struct hw_console_session *next;
} hw_console_session_t;

/* ---- 驱动状态：单实例。会话保持已分配，destroy 时统一释放 ---- */
static hw_console_session_t *g_sessions = NULL;
static uint64_t g_sess_seq = 0;

/* 供 ops 表引用的内部实现（前置声明，避免 -Wmissing-prototypes） */
static int console_open(const char *name, hw_console_session_t **out);
static int console_close(hw_console_session_t *session);
static int console_write(hw_console_session_t *session, const char *buf, size_t len);
static int console_read_line(hw_console_session_t *session, char *buf, size_t cap, size_t *n);
static int console_banner(hw_console_session_t *session, const char *text);
static int console_clear(hw_console_session_t *session);
static int console_status(hw_console_session_t *session, hw_console_info_t *out);
static int console_list(hw_console_info_t *out, int cap, int *count);
static int console_bind(hw_console_session_t *session,
                        void (*cb)(const void *ctx, const char *data, size_t len), const void *ctx);

/* ---- 会话内部工具 ---- */

/* 往行缓冲追加 n 字节（含末尾 NUL） */
static int sess_append(hw_console_session_t *s, const char *data, size_t len) {
    if (s->len + len + 1 > s->cap) {
        size_t ncap = s->cap ? s->cap : 64;
        while (ncap < s->len + len + 1)
            ncap *= 2;
        char *nb = (char *)realloc(s->buf, ncap);
        if (!nb) return -ENOMEM;
        s->buf = nb;
        s->cap = ncap;
    }
    memcpy(s->buf + s->len, data, len);
    s->len += len;
    s->buf[s->len] = '\0';
    s->written += (uint64_t)len;
    return 0;
}

/* 统计当前可读完整行数 */
static uint64_t sess_count_lines(const hw_console_session_t *s) {
    uint64_t c = 0;
    for (size_t i = 0; i < s->len; i++)
        if (s->buf[i] == '\n') c++;
    return c;
}

/* 释放全部会话（stop/destroy 调用） */
void hw_console_cleanup(void) {
    hw_console_session_t *it = g_sessions;
    while (it) {
        hw_console_session_t *nx = it->next;
        free(it->buf);
        free(it);
        it = nx;
    }
    g_sessions = NULL;
    g_sess_seq = 0;
}

/* ---- ops 实现 ---- */

static int32_t console_version(void) {
    return 1;
}

static int console_open(const char *name, hw_console_session_t **out) {
    if (!name || !out) return -EINVAL;
    size_t nlen = strlen(name);
    if (nlen == 0 || nlen >= HW_CONSOLE_NAME_MAX) return -EINVAL;
    for (hw_console_session_t *it = g_sessions; it; it = it->next)
        if (it->active && strcmp(it->name, name) == 0) return -EEXIST;

    hw_console_session_t *s = (hw_console_session_t *)calloc(1, sizeof(*s));
    if (!s) return -ENOMEM;
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->id = ++g_sess_seq;
    s->active = 1;
    s->next = g_sessions;
    g_sessions = s;
    *out = s;
    return 0;
}

static int console_close(hw_console_session_t *session) {
    hw_console_session_t *s = (hw_console_session_t *)session;
    if (!s) return -EINVAL;
    if (!s->active) return -EINVAL; /* 已关闭 */
    s->active = 0; /* 标记关闭并移出活动集合，内存保有至 cleanup */
    return 0;
}

static int console_write(hw_console_session_t *session, const char *buf, size_t len) {
    hw_console_session_t *s = (hw_console_session_t *)session;
    if (!s || !s->active) return -EINVAL;
    if (!buf) return -EINVAL;
    int rc = sess_append(s, buf, len);
    if (rc != 0) return rc;
    if (s->out_cb) s->out_cb(s->out_ctx, buf, len);
    return 0;
}

static int console_read_line(hw_console_session_t *session, char *buf, size_t cap, size_t *n) {
    hw_console_session_t *s = (hw_console_session_t *)session;
    if (!s || !s->active) return -EINVAL;
    if (!buf || !cap || !n) return -EINVAL;
    *n = 0;
    const char *nl = (const char *)memchr(s->buf, '\n', s->len);
    if (!nl) return -EAGAIN;            /* 尚无完整行 */
    size_t pos = (size_t)(nl - s->buf); /* 首个换行位置 */
    if (pos >= cap) return -ENOBUFS;    /* 一行放不下：不消费 */
    memcpy(buf, s->buf, pos);
    buf[pos] = '\0';
    *n = pos;
    /* 消费该行（含 '\n'） */
    size_t rest = s->len - (pos + 1);
    if (rest) {
        memmove(s->buf, s->buf + pos + 1, rest);
        s->len = rest;
        s->buf[rest] = '\0';
    } else {
        free(s->buf);
        s->buf = NULL;
        s->len = 0;
        s->cap = 0;
    }
    return 0;
}

static int console_banner(hw_console_session_t *session, const char *text) {
    hw_console_session_t *s = (hw_console_session_t *)session;
    if (!s || !s->active) return -EINVAL;
    if (!text) return -EINVAL;
    /* 清空现有行缓冲，banner 置首行 */
    free(s->buf);
    s->buf = NULL;
    s->len = 0;
    s->cap = 0;
    s->written = 0;
    /* 转义换行，再补行尾换行作为独立首行 */
    size_t tlen = strlen(text);
    char *tb = (char *)malloc(tlen * 2 + 2);
    if (!tb) return -ENOMEM;
    size_t o = 0;
    for (size_t i = 0; i < tlen; i++) {
        if (text[i] == '\n') {
            tb[o++] = '\\';
            tb[o++] = 'n';
        } else {
            tb[o++] = text[i];
        }
    }
    tb[o] = '\0';
    int rc = sess_append(s, tb, o);
    free(tb);
    if (rc != 0) return rc;
    rc = sess_append(s, "\n", 1);
    if (rc != 0) return rc;
    if (s->out_cb) s->out_cb(s->out_ctx, text, tlen);
    return 0;
}

static int console_clear(hw_console_session_t *session) {
    hw_console_session_t *s = (hw_console_session_t *)session;
    if (!s || !s->active) return -EINVAL;
    free(s->buf);
    s->buf = NULL;
    s->len = 0;
    s->cap = 0;
    return 0;
}

static int console_status(hw_console_session_t *session, hw_console_info_t *out) {
    hw_console_session_t *s = (hw_console_session_t *)session;
    if (!s || !s->active) return -EINVAL;
    if (!out) return -EINVAL;
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", s->name);
    out->id = s->id;
    out->active = s->active;
    out->lines = sess_count_lines(s);
    out->written = s->written;
    out->buffered = s->len;
    return 0;
}

static int console_list(hw_console_info_t *out, int cap, int *count) {
    if (!out || !count || cap <= 0) return -EINVAL;
    int n = 0;
    for (hw_console_session_t *it = g_sessions; it && n < cap; it = it->next) {
        if (!it->active) continue;
        memset(&out[n], 0, sizeof(out[0]));
        snprintf(out[n].name, sizeof(out[n].name), "%s", it->name);
        out[n].id = it->id;
        out[n].active = it->active;
        out[n].lines = sess_count_lines(it);
        out[n].written = it->written;
        out[n].buffered = it->len;
        n++;
    }
    *count = n;
    return 0;
}

static int console_bind(hw_console_session_t *session,
                        void (*cb)(const void *ctx, const char *data, size_t len),
                        const void *ctx) {
    hw_console_session_t *s = (hw_console_session_t *)session;
    if (!s) return -EINVAL;
    s->out_cb = cb;
    s->out_ctx = ctx;
    return 0;
}

/* ---- ops 表 ---- */
hw_console_ops_t hw_console_ops = {
    .version = console_version,
    .open = console_open,
    .close = console_close,
    .write = console_write,
    .read_line = console_read_line,
    .banner = console_banner,
    .clear = console_clear,
    .status = console_status,
    .list_sessions = console_list,
    .bind_output = console_bind,
};

#ifdef __cplusplus
}
#endif