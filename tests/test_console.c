/*
 * test_console.c — CMocka 单元测试：CONSOLE 系统控制台协议（dlopen .so 后真实调用）
 *
 * 测试对象：console/build/console.so 插件及其 provides 协议 "CONSOLE" 的
 * hw_console_ops_t（内存行缓冲的读终端抽象，零内核依赖）。CONSOLE 的 requires
 * 含 LOG/PARAM/METAPROTO，但实现自包含，故单独 dlopen 亦可真跑。
 *
 * 链路（与 test_compress.c 一致）：
 *   dlopen(.so) -> hw_plugin_entry() -> init()
 *   -> metaproto_register(provides, implementation=get_interface("CONSOLE"))
 *   -> start -> metaproto_resolve("CONSOLE") -> hw_console_ops_t*
 *
 * 覆盖要点（全部真实调用）：
 *   - version：协议版本返回 1；
 *   - open：非法入参 -EINVAL；同名活动会话 -EEXIST；关闭后同名可重开；
 *   - write + read_line：多行写入逐行弹出往返 == 原文；
 *   - banner：'\n' 转义为字面 "\n" 并置为行缓冲首行；
 *   - clear：清空后 read_line 返回 -EAGAIN，status.buffered == 0；
 *   - close：关闭后 write/read_line/status 返回负 errno；
 *   - list_sessions：含/不含（关闭者不再列出）；
 *   - status：lines/written/active 报告正确；
 *   - bind_output：绑定回调后 write/banner 逐次回调挂接终端。
 *
 * 错误约定：成功返回 0，失败返回负 errno。清理：stop -> unregister -> destroy
 * -> dlclose；p 是 .so 静态描述符不得 free。
 */

#include "hwrun.h"
#include "bus.h"
#include "metaproto.h"
#include "../console/include/console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <setjmp.h>
#include <dlfcn.h>
#include <cmocka.h>

#ifndef HWRUN_PLUGIN_ROOT
#define HWRUN_PLUGIN_ROOT "../"
#endif

static hw_bus_t g_bus;
static void *g_h = NULL; /* dlopen 句柄 */
static hw_plugin_t *g_p = NULL;
static hw_console_ops_t *g_ops = NULL;

/* 挂接终端输出回调：累计接收片段 */
static size_t g_out_bytes = 0;
static char g_out_buf[256];
static int g_out_calls = 0;
static void out_sink(const void *ctx, const char *data, size_t len) {
    (void)ctx;
    if (len > sizeof(g_out_buf) - 1 - g_out_bytes) len = sizeof(g_out_buf) - 1 - g_out_bytes;
    memcpy(g_out_buf + g_out_bytes, data, len);
    g_out_bytes += len;
    g_out_calls++;
}

/* ---- 组级 setup：总线 + dlopen + 生命周期 + 注册 provides ---- */
static int group_setup(void **state) {
    (void)state;
    if (hw_bus_init(&g_bus, NULL, NULL, HWLOG_WARN) != HWRUN_OK) {
        fprintf(stderr, "test_console: hw_bus_init 失败\n");
        return -1;
    }

    char so[512];
    snprintf(so, sizeof(so), HWRUN_PLUGIN_ROOT "console/build/console.so");
    g_h = dlopen(so, RTLD_NOW | RTLD_GLOBAL);
    if (!g_h) {
        printf("  [skip] dlopen(%s): %s\n", so, dlerror());
        return 0;
    }

    hw_plugin_t *(*entry)(void) = (hw_plugin_t * (*)(void)) dlsym(g_h, "hw_plugin_entry");
    if (!entry) {
        printf("  [skip] %s: 无 hw_plugin_entry\n", so);
        dlclose(g_h);
        g_h = NULL;
        return 0;
    }
    hw_plugin_t *self = entry();
    if (!self) {
        dlclose(g_h);
        g_h = NULL;
        return 0;
    }

    if (self->ops.init) self->ops.init(self);
    self->state = HWPLUGIN_LOADED;

    for (int i = 0; i < self->provides_count; i++) {
        void *impl = self->ops.get_interface ? self->ops.get_interface(self->provides[i]) : NULL;
        hw_metaproto_register(&g_bus.meta, self->provides[i], HWRUN_PROTOCOL_VERSION, self->id,
                              impl ? impl : (void *)self);
    }
    if (self->ops.start) self->ops.start(self);
    self->state = HWPLUGIN_STARTED;
    g_p = self;

    hw_protocol_route_t *r = NULL;
    if (hw_metaproto_resolve(&g_bus.meta, HWPROTO_CONSOLE, NULL, &r) == HWRUN_OK && r &&
        r->implementation)
        g_ops = (hw_console_ops_t *)r->implementation;
    else
        printf("  [warn] resolve(CONSOLE) 未取得实现指针\n");
    return 0;
}

static int group_teardown(void **state) {
    (void)state;
    if (!g_p) return 0;
    if (g_p->ops.stop) g_p->ops.stop(g_p);
    for (int i = 0; i < g_p->provides_count; i++)
        hw_metaproto_unregister(&g_bus.meta, g_p->provides[i], g_p->id);
    if (g_p->ops.destroy) g_p->ops.destroy(g_p);
    dlclose(g_h);
    g_h = NULL;
    g_p = NULL;
    g_ops = NULL;
    hw_bus_shutdown(&g_bus);
    return 0;
}

static hw_console_session_t *open_named(const char *name) {
    hw_console_session_t *s = NULL;
    assert_int_equal(g_ops->open(name, &s), 0);
    assert_non_null(s);
    return s;
}

static void test_console_interface(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    assert_non_null(g_ops->open);
    assert_non_null(g_ops->close);
    assert_int_equal(g_ops->version(), 1);
    printf("  [info] ops bound via metaproto(CONSOLE), version=%d\n", g_ops->version());
}

static void test_console_open_duplicate(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    hw_console_session_t *a = open_named("tty0");
    hw_console_session_t *b = NULL;
    /* 同名活动会话 -> EEXIST */
    assert_int_equal(g_ops->open("tty0", &b), -EEXIST);
    assert_null(b);
    printf("  [info] open(tty0) dup -> -EEXIST\n");
    assert_int_equal(g_ops->close(a), 0);
    /* 关闭后可重开同名 */
    hw_console_session_t *c = NULL;
    assert_int_equal(g_ops->open("tty0", &c), 0);
    assert_non_null(c);
    assert_int_equal(g_ops->close(c), 0);
    /* 非法入参 */
    assert_int_equal(g_ops->open(NULL, &b), -EINVAL);
    assert_int_equal(g_ops->open("", &b), -EINVAL);
}

static void test_console_write_read_line(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    hw_console_session_t *s = open_named("tty1");
    assert_int_equal(g_ops->write(s, "hello world\n", 12), 0);
    assert_int_equal(g_ops->write(s, "second line\n", 12), 0);
    char line[64];
    size_t n = 0;
    assert_int_equal(g_ops->read_line(s, line, sizeof(line), &n), 0);
    assert_int_equal(n, 11);
    assert_string_equal(line, "hello world");
    assert_int_equal(g_ops->read_line(s, line, sizeof(line), &n), 0);
    assert_string_equal(line, "second line");
    /* 无完整行 -> EAGAIN（仍有残行无换行） */
    assert_int_equal(g_ops->write(s, "partial", 7), 0);
    assert_int_equal(g_ops->read_line(s, line, sizeof(line), &n), -EAGAIN);
    assert_int_equal(g_ops->close(s), 0);
    printf("  [info] write/read_line roundtrip ok\n");
}

static void test_console_banner_first_line(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    hw_console_session_t *s = open_named("tty2");
    /* banner 含换行：应被转义为字面 "\n" 并置为首行 */
    assert_int_equal(g_ops->banner(s, "My Console\nv1"), 0);
    char line[128];
    size_t n = 0;
    assert_int_equal(g_ops->read_line(s, line, sizeof(line), &n), 0);
    assert_string_equal(line, "My Console\\nv1"); /* 字面反斜杠 + 'n' */
    printf("  [info] banner escaped to first line: %s\n", line);
    assert_int_equal(g_ops->close(s), 0);
}

static void test_console_clear(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    hw_console_session_t *s = open_named("tty3");
    assert_int_equal(g_ops->write(s, "data line\n", 10), 0);
    assert_int_equal(g_ops->clear(s), 0);
    char line[64];
    size_t n = 0;
    assert_int_equal(g_ops->read_line(s, line, sizeof(line), &n), -EAGAIN);
    assert_int_equal(n, 0);
    hw_console_info_t st;
    assert_int_equal(g_ops->status(s, &st), 0);
    assert_int_equal(st.lines, 0);
    assert_int_equal(st.buffered, 0);
    assert_int_equal(g_ops->close(s), 0);
}

static void test_console_close_invalid_ops(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    hw_console_session_t *s = open_named("tty4");
    assert_int_equal(g_ops->close(s), 0);
    /* 关闭后读写/状态返回负 errno */
    assert_int_equal(g_ops->write(s, "x", 1), -EINVAL);
    char line[64];
    size_t n = 0;
    assert_int_equal(g_ops->read_line(s, line, sizeof(line), &n), -EINVAL);
    hw_console_info_t st;
    assert_int_equal(g_ops->status(s, &st), -EINVAL);
    assert_int_equal(g_ops->clear(s), -EINVAL);
    /* 重复关闭亦负 errno */
    assert_int_equal(g_ops->close(s), -EINVAL);
    assert_int_equal(g_ops->read_line(NULL, line, sizeof(line), &n), -EINVAL);
}

static void test_console_list_sessions(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    hw_console_session_t *a = open_named("ttyA");
    hw_console_session_t *b = open_named("ttyB");
    hw_console_session_t *c = open_named("ttyC");
    (void)a;
    (void)b;
    (void)c;
    hw_console_info_t info[8];
    int count = 0;
    assert_int_equal(g_ops->list_sessions(info, 8, &count), 0);
    assert_int_equal(count, 3);
    int has_a = 0, has_c = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(info[i].name, "ttyA") == 0) has_a = 1;
        if (strcmp(info[i].name, "ttyC") == 0) has_c = 1;
    }
    assert_int_equal(has_a, 1);
    assert_int_equal(has_c, 1);
    /* 关闭 ttyA 后不再列出 */
    assert_int_equal(g_ops->close(a), 0);
    assert_int_equal(g_ops->list_sessions(info, 8, &count), 0);
    assert_int_equal(count, 2);
    int still_a = 0;
    for (int i = 0; i < count; i++)
        if (strcmp(info[i].name, "ttyA") == 0) still_a = 1;
    assert_int_equal(still_a, 0);
    printf("  [info] list_sessions includes ttyA/ttyB/ttyC, drops closed ttyA\n");
    assert_int_equal(g_ops->close(b), 0);
    assert_int_equal(g_ops->close(c), 0);
}

static void test_console_status_report(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    hw_console_session_t *s = open_named("ttyS0");
    assert_int_equal(g_ops->write(s, "line one\n", 9), 0);
    assert_int_equal(g_ops->write(s, "line two\n", 9), 0);
    hw_console_info_t st;
    assert_int_equal(g_ops->status(s, &st), 0);
    assert_int_equal(st.active, 1);
    assert_int_equal(st.lines, 2);
    assert_int_equal(st.written, 18);
    assert_int_equal(st.buffered, 18);
    assert_string_equal(st.name, "ttyS0");
    printf("  [info] status: %s lines=%llu written=%llu buffered=%zu\n", st.name,
           (unsigned long long)st.lines, (unsigned long long)st.written, st.buffered);
    assert_int_equal(g_ops->close(s), 0);
}

static void test_console_bind_output(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    memset(g_out_buf, 0, sizeof(g_out_buf));
    g_out_bytes = 0;
    g_out_calls = 0;
    hw_console_session_t *s = open_named("con");
    assert_int_equal(g_ops->bind_output(s, out_sink, s), 0);
    assert_int_equal(g_ops->write(s, "Hi ", 3), 0);
    assert_int_equal(g_ops->write(s, "Console\n", 8), 0);
    assert_int_equal(g_out_calls, 2);
    assert_int_equal(g_out_bytes, 11);
    assert_string_equal(g_out_buf, "Hi Console\n"); /* 挂接终端逐段接收 */
    /* banner 亦回调原始文本 */
    memset(g_out_buf, 0, sizeof(g_out_buf));
    g_out_bytes = 0;
    assert_int_equal(g_ops->banner(s, "Greetings"), 0);
    assert_int_equal(g_out_bytes, 9);
    assert_string_equal(g_out_buf, "Greetings");
    /* 解绑后不再回调 */
    assert_int_equal(g_ops->bind_output(s, NULL, NULL), 0);
    g_out_calls = 0;
    assert_int_equal(g_ops->write(s, "silent", 6), 0);
    assert_int_equal(g_out_calls, 0);
    printf("  [info] bind_output callback mirror ok\n");
    assert_int_equal(g_ops->close(s), 0);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_console_interface),
        cmocka_unit_test(test_console_open_duplicate),
        cmocka_unit_test(test_console_write_read_line),
        cmocka_unit_test(test_console_banner_first_line),
        cmocka_unit_test(test_console_clear),
        cmocka_unit_test(test_console_close_invalid_ops),
        cmocka_unit_test(test_console_list_sessions),
        cmocka_unit_test(test_console_status_report),
        cmocka_unit_test(test_console_bind_output),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}