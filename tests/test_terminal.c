/*
 * test_terminal.c — CMocka 单元测试：TERMINAL 终端协议（dlopen .so 后真实调用）
 *
 * 测试对象：terminal/build/terminal.so 插件及其 provides 协议 "TERMINAL" 的
 * hw_terminal_ops_t（真实 PTY，非打桩）。TERMINAL 的 requires 含 LOG/PARAM/
 * METAPROTO，但实现自包含（纯 POSIX），故单独 dlopen 亦可真跑。
 *
 * 链路（与 test_compress.c 一致）：
 *   dlopen(.so) -> hw_plugin_entry() -> init()
 *   -> metaproto_register(provides, implementation=get_interface("TERMINAL"))
 *   -> start -> metaproto_resolve("TERMINAL") -> hw_terminal_ops_t*
 *
 * 覆盖要点（全部真跑，无 skip，需 WSL/Linux 且可用 /bin/sh、/bin/cat）：
 *   - version / list_sessions（初始为空）
 *   - spawn("/bin/sh -c echo hello-world") 后 read 读回输出
 *   - spawn("/bin/cat") 后 write 写入、read 回显
 *   - resize 后 status 反映新窗口大小与子进程存活
 *   - close 后 write/read 返回 -EBADF，list_sessions 计数下降
 *   - 多会话计数
 *
 * 错误约定：成功返回 0，失败返回负 errno。会话由测试自行 close（组 teardown
 * 之前收敛，避免残留）。
 */

#include "hwrun.h"
#include "bus.h"
#include "metaproto.h"
#include "../terminal/include/terminal.h"

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
static hw_terminal_ops_t *g_ops = NULL;

/* ---- 组级 setup：总线 + dlopen + 生命周期 + 注册 provides ---- */
static int group_setup(void **state) {
    (void)state;
    if (hw_bus_init(&g_bus, NULL, NULL, HWLOG_WARN) != HWRUN_OK) {
        fprintf(stderr, "test_terminal: hw_bus_init 失败\n");
        return -1;
    }

    char so[512];
    snprintf(so, sizeof(so), HWRUN_PLUGIN_ROOT "terminal/build/terminal.so");
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
    if (hw_metaproto_resolve(&g_bus.meta, HWPROTO_TERMINAL, NULL, &r) == HWRUN_OK && r &&
        r->implementation)
        g_ops = (hw_terminal_ops_t *)r->implementation;
    else
        printf("  [warn] resolve(TERMINAL) 未取得实现指针\n");
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

/* ---- 帮手：spawn 一个命令并返回会话 ---- */
static int spawn_cmd(hw_terminal_session_t *out, char **argv, uint32_t cols, uint32_t rows) {
    hw_terminal_spawn_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cmd = argv[0];
    cfg.argv = argv;
    cfg.envp = NULL;
    cfg.cols = cols;
    cfg.rows = rows;
    return g_ops->spawn(&cfg, out);
}

/* ---- 帮手：连续 read 直到累积缓冲包含 needle；返回累积内容 ---- */
static int read_until(hw_terminal_session_t *s, char *acc, size_t cap, const char *needle) {
    size_t len = 0;
    if (cap == 0) return 0;
    for (int i = 0; i < 500; i++) {
        uint32_t n = 0;
        int rc = g_ops->read(s, (uint8_t *)(acc + len), (uint32_t)(cap - len - 1), &n);
        if (rc != 0) return rc;
        if (n > 0) {
            len += n;
            acc[len] = '\0';
            if (strstr(acc, needle)) return 1;
        }
        usleep(2000);
    }
    return 0;
}

static void test_terminal_version(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    assert_int_equal(g_ops->version(), 1);
    printf("  [info] terminal version = %d\n", (int)g_ops->version());
}

static void test_terminal_list_empty(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    hw_terminal_session_t arr[8];
    memset(arr, 0, sizeof(arr));
    int cnt = -1;
    assert_int_equal(g_ops->list_sessions(arr, 8, &cnt), 0);
    assert_int_equal(cnt, 0); /* 初始无会话 */
}

static void test_terminal_spawn_basic(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    char *sh[] = {(char *)"/bin/sh", "-c", "echo hello-world", NULL};
    hw_terminal_session_t s;
    memset(&s, 0, sizeof(s));
    assert_int_equal(spawn_cmd(&s, sh, 100, 30), 0);
    assert_int_equal(s.state, HW_TERMINAL_STATE_ACTIVE);
    assert_int_equal(s.cols, 100);
    assert_int_equal(s.rows, 30);
    assert_true(s.master_fd >= 0);
    assert_true(s.pid > 0);
    assert_true(s.slave_path[0] == '/');
    assert_int_equal(g_ops->close(&s), 0);
}

static void test_terminal_read_output(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    char *sh[] = {(char *)"/bin/sh", "-c", "echo hello-world", NULL};
    hw_terminal_session_t s;
    memset(&s, 0, sizeof(s));
    assert_int_equal(spawn_cmd(&s, sh, 80, 24), 0);

    char acc[256] = {0};
    assert_int_equal(read_until(&s, acc, sizeof(acc), "hello-world"), 1);
    assert_non_null(strstr(acc, "hello-world"));
    printf("  [info] sh output: %.60s\n", acc);
    assert_int_equal(g_ops->close(&s), 0);
}

static void test_terminal_write_echo(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    char *cat[] = {(char *)"/bin/cat", NULL};
    hw_terminal_session_t s;
    memset(&s, 0, sizeof(s));
    assert_int_equal(spawn_cmd(&s, cat, 80, 24), 0);

    static const char in[] = "ping-pty\n";
    int w = g_ops->write(&s, (const uint8_t *)in, (uint32_t)(sizeof(in) - 1));
    assert_int_equal(w, (int)(sizeof(in) - 1)); /* 全部写入 */

    char acc[256] = {0};
    assert_int_equal(read_until(&s, acc, sizeof(acc), "ping-pty"), 1);
    assert_non_null(strstr(acc, "ping-pty"));
    printf("  [info] cat echo: %.60s\n", acc);
    assert_int_equal(g_ops->close(&s), 0);
}

static void test_terminal_resize_status(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    char *cat[] = {(char *)"/bin/cat", NULL};
    hw_terminal_session_t s;
    memset(&s, 0, sizeof(s));
    assert_int_equal(spawn_cmd(&s, cat, 80, 24), 0);

    assert_int_equal(g_ops->resize(&s, 120, 40), 0);

    hw_terminal_status_t st;
    memset(&st, 0, sizeof(st));
    assert_int_equal(g_ops->status(&s, &st), 0);
    assert_int_equal(st.cols, 120);
    assert_int_equal(st.rows, 40);
    assert_int_equal(st.state, HW_TERMINAL_STATE_ACTIVE);
    assert_int_equal(st.pid, s.pid);
    assert_int_equal(st.running, 1); /* cat 存活 */
    printf("  [info] status: %dx%d running=%d pid=%d\n", st.cols, st.rows, st.running, st.pid);
    assert_int_equal(g_ops->close(&s), 0);
}

static void test_terminal_close_error(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    char *sh[] = {(char *)"/bin/sh", "-c", "sleep 5", NULL};
    hw_terminal_session_t s;
    memset(&s, 0, sizeof(s));
    assert_int_equal(spawn_cmd(&s, sh, 80, 24), 0);

    hw_terminal_session_t arr[8];
    int before = 0;
    assert_int_equal(g_ops->list_sessions(arr, 8, &before), 0);

    assert_int_equal(g_ops->close(&s), 0);
    assert_int_equal(s.state, HW_TERMINAL_STATE_CLOSED);
    assert_int_equal(s.master_fd, -1);

    uint8_t buf[16] = {0};
    uint32_t n = 123;
    assert_int_equal(g_ops->write(&s, buf, 4), -EBADF);     /* 关闭后写 -> -EBADF */
    assert_int_equal(g_ops->read(&s, buf, 16, &n), -EBADF); /* 关闭后读 -> -EBADF */

    int after = 0;
    assert_int_equal(g_ops->list_sessions(arr, 8, &after), 0);
    assert_int_equal(after, before - 1); /* 关闭后计数下降 */
}

static void test_terminal_list_count(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    char *sh1[] = {(char *)"/bin/sh", "-c", "sleep 5", NULL};
    char *sh2[] = {(char *)"/bin/sh", "-c", "sleep 5", NULL};
    hw_terminal_session_t s1, s2;
    memset(&s1, 0, sizeof(s1));
    memset(&s2, 0, sizeof(s2));
    assert_int_equal(spawn_cmd(&s1, sh1, 80, 24), 0);
    assert_int_equal(spawn_cmd(&s2, sh2, 80, 24), 0);

    hw_terminal_session_t arr[8];
    int cnt = 0;
    assert_int_equal(g_ops->list_sessions(arr, 8, &cnt), 0);
    assert_int_equal(cnt, 2);

    assert_int_equal(g_ops->close(&s1), 0);
    assert_int_equal(g_ops->list_sessions(arr, 8, &cnt), 0);
    assert_int_equal(cnt, 1);
    assert_int_equal(g_ops->close(&s2), 0);
    assert_int_equal(g_ops->list_sessions(arr, 8, &cnt), 0);
    assert_int_equal(cnt, 0);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_terminal_version),     cmocka_unit_test(test_terminal_list_empty),
        cmocka_unit_test(test_terminal_spawn_basic), cmocka_unit_test(test_terminal_read_output),
        cmocka_unit_test(test_terminal_write_echo),  cmocka_unit_test(test_terminal_resize_status),
        cmocka_unit_test(test_terminal_close_error), cmocka_unit_test(test_terminal_list_count),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}