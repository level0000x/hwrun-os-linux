/*
 * test_shell.c — CMocka 单元测试：SHELL 命令行解释器（dlopen .so 后真实调用）
 *
 * 测试对象：shell/build/shell.so 插件及其 provides 协议 "SHELL" 的
 * hw_shell_ops_t（真实用户态实现，非打桩）。SHELL 的 requires 含 LOG/PARAM/
 * METAPROTO，但实现自包含（只用 libc + POSIX pipe/fork/exec），故单独 dlopen 亦可真跑。
 *
 * 链路（与 test_compress.c 一致）：
 *   dlopen(.so) -> hw_plugin_entry() -> init()
 *   -> metaproto_register(provides, implementation=get_interface("SHELL"))
 *   -> start -> metaproto_resolve("SHELL") -> hw_shell_ops_t*
 *
 * 覆盖要点（全部真跑，不 skip）：
 *   - 内置命令：help 命中、eval("echo ...") 捕获、pwd 非空、exit/quit；
 *   - 历史：eval 自动追加，get/list/clear/count 正确；
 *   - 别名：set/get/clear，eval 对命令首词单层展开；
 *   - 外部命令：非内置行经 /bin/sh -c 捕获（printf —— 非本插件内置名，走外部路径）；
 *   - 错误路径：NULL/空行/全空白/非法 -> 负 errno。
 *
 * 错误约定：成功返回 HWRUN_OK(0)，失败返回负 errno。
 * 清理：stop -> unregister -> destroy -> dlclose；p 是 .so 静态描述符不得 free。
 */

#include "hwrun.h"
#include "bus.h"
#include "metaproto.h"
#include "../shell/include/shell.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <setjmp.h>
#include <dlfcn.h>
#include <cmocka.h>

#ifndef HWRUN_PLUGIN_ROOT
#define HWRUN_PLUGIN_ROOT "../"
#endif

static hw_bus_t g_bus;
static void *g_h = NULL; /* dlopen 句柄            */
static hw_plugin_t *g_p = NULL;
static hw_shell_ops_t *g_ops = NULL;

/* ---- 组级 setup：总线 + dlopen + 生命周期 + 注册 provides ---- */
static int group_setup(void **state) {
    (void)state;
    if (hw_bus_init(&g_bus, NULL, NULL, HWLOG_WARN) != HWRUN_OK) {
        fprintf(stderr, "test_shell: hw_bus_init 失败\n");
        return -1;
    }

    char so[512];
    snprintf(so, sizeof(so), HWRUN_PLUGIN_ROOT "shell/build/shell.so");
    g_h = dlopen(so, RTLD_NOW | RTLD_GLOBAL);
    if (!g_h) {
        printf("  [error] dlopen(%s): %s\n", so, dlerror());
        return 0;
    }

    hw_plugin_t *(*entry)(void) = (hw_plugin_t * (*)(void)) dlsym(g_h, "hw_plugin_entry");
    if (!entry) {
        printf("  [error] %s: 无 hw_plugin_entry\n", so);
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
    if (hw_metaproto_resolve(&g_bus.meta, HWPROTO_SHELL, NULL, &r) == HWRUN_OK && r &&
        r->implementation)
        g_ops = (hw_shell_ops_t *)r->implementation;
    else
        printf("  [warn] resolve(SHELL) 未取得实现指针\n");
    return 0;
}

static int group_teardown(void **state) {
    (void)state;
    if (g_p) {
        if (g_p->ops.stop) g_p->ops.stop(g_p);
        for (int j = 0; j < g_p->provides_count; j++)
            hw_metaproto_unregister(&g_bus.meta, g_p->provides[j], g_p->id);
        if (g_p->ops.destroy) g_p->ops.destroy(g_p);
    }
    if (g_h) dlclose(g_h);
    g_p = NULL;
    g_h = NULL;
    g_ops = NULL;
    hw_bus_shutdown(&g_bus);
    return 0;
}

/* ============================================================
 * 用例
 * ============================================================ */

/* 版本 + 状态摘要 */
static void test_shell_version_status(void **state) {
    (void)state;
    char out[512];
    size_t n = 0;
    assert_true(g_ops->version() > 0);
    assert_int_equal(g_ops->status(out, sizeof(out), &n), HWRUN_OK);
    assert_true(n > 0);
    assert_non_null(strstr(out, "SHELL1.0"));
    assert_non_null(strstr(out, "builtins="));
    printf("  [info] status: %s", out);
}

/* 内置命令：help 命中 / echo 捕获 / exit 与 quit */
static void test_shell_builtin_help_echo(void **state) {
    (void)state;
    char out[4096];
    size_t n = 0;
    assert_int_equal(g_ops->eval("help", out, sizeof(out), &n), HWRUN_OK);
    assert_non_null(strstr(out, "echo"));
    assert_non_null(strstr(out, "pwd"));
    assert_non_null(strstr(out, "alias"));

    n = 0;
    assert_int_equal(g_ops->eval("echo hello", out, sizeof(out), &n), HWRUN_OK);
    assert_non_null(strstr(out, "hello"));

    n = 0;
    assert_int_equal(g_ops->eval("echo a   b c", out, sizeof(out), &n), HWRUN_OK);
    assert_non_null(strstr(out, "a b c"));

    n = 0;
    assert_int_equal(g_ops->eval("exit", out, sizeof(out), &n), HWRUN_OK);
    assert_non_null(strstr(out, "bye"));
    n = 0;
    assert_int_equal(g_ops->eval("quit", out, sizeof(out), &n), HWRUN_OK);
    assert_non_null(strstr(out, "bye"));
}

/* pwd：非空且含 '/' */
static void test_shell_builtin_pwd(void **state) {
    (void)state;
    char out[512];
    size_t n = 0;
    assert_int_equal(g_ops->eval("pwd", out, sizeof(out), &n), HWRUN_OK);
    assert_true(n > 0);
    assert_non_null(strchr(out, '/'));
}

/* 历史：eval 自动追加，get/list/clear/count 正确 */
static void test_shell_history(void **state) {
    (void)state;
    assert_int_equal(g_ops->history_clear(), HWRUN_OK);
    char out[4096];
    size_t n = 0;

    assert_int_equal(g_ops->eval("echo one", out, sizeof(out), &n), HWRUN_OK);
    assert_int_equal(g_ops->eval("echo two", out, sizeof(out), &n), HWRUN_OK);
    assert_int_equal(g_ops->history_count(), 2);

    char line[512];
    assert_int_equal(g_ops->history_get(0, line, sizeof(line)), HWRUN_OK);
    assert_string_equal(line, "echo one");
    assert_int_equal(g_ops->history_get(1, line, sizeof(line)), HWRUN_OK);
    assert_string_equal(line, "echo two");
    assert_int_equal(g_ops->history_get(2, line, sizeof(line)), -ENOENT);
    assert_int_equal(g_ops->history_get(-1, line, sizeof(line)), -ENOENT);

    n = 0;
    assert_int_equal(g_ops->history_list(out, sizeof(out), &n), HWRUN_OK);
    assert_non_null(strstr(out, "echo one"));
    assert_non_null(strstr(out, "echo two"));

    assert_int_equal(g_ops->history_clear(), HWRUN_OK);
    assert_int_equal(g_ops->history_count(), 0);
}

/* 别名：set/get/clear，eval 对命令首词展开 */
static void test_shell_alias(void **state) {
    (void)state;
    char out[4096];
    size_t n = 0;

    assert_int_equal(g_ops->alias_set("ll", "echo aliased-ll"), HWRUN_OK);
    assert_int_equal(g_ops->alias_get("ll", out, sizeof(out)), HWRUN_OK);
    assert_string_equal(out, "echo aliased-ll");
    assert_int_equal(g_ops->alias_count(), 1);

    /* eval 遇到别名首词 -> 展开执行 */
    n = 0;
    assert_int_equal(g_ops->eval("ll extra", out, sizeof(out), &n), HWRUN_OK);
    assert_non_null(strstr(out, "aliased-ll extra"));

    assert_int_equal(g_ops->alias_clear(), HWRUN_OK);
    assert_int_equal(g_ops->alias_count(), 0);
    assert_int_equal(g_ops->alias_get("ll", out, sizeof(out)), -ENOENT);
}

/* 外部命令：printf 非本插件内置名 -> 经 /bin/sh -c 捕获 */
static void test_shell_external_command(void **state) {
    (void)state;
    char out[4096];
    size_t n = 0;
    assert_int_equal(g_ops->eval("printf 'hw-external-ok'", out, sizeof(out), &n), HWRUN_OK);
    assert_non_null(strstr(out, "hw-external-ok"));
}

/* 错误路径：NULL / 空行 / 全空白 / 非法 out -> 负 errno */
static void test_shell_error_paths(void **state) {
    (void)state;
    char out[512];
    size_t n = 0;
    assert_true(g_ops->eval(NULL, out, sizeof(out), &n) < 0);
    assert_true(g_ops->eval("", out, sizeof(out), &n) < 0);
    assert_true(g_ops->eval("   \t  ", out, sizeof(out), &n) < 0);
    assert_true(g_ops->eval("echo hi", NULL, 0, &n) < 0);
    assert_true(g_ops->history_append(NULL) < 0);
    assert_true(g_ops->history_get(0, NULL, 0) < 0);
}

/* run：argv 直接执行（内置词）返回 0 且不入历史 */
static void test_shell_run(void **state) {
    (void)state;
    assert_int_equal(g_ops->history_clear(), HWRUN_OK);
    char *argv1[] = {"echo", "run-ok", NULL};
    assert_int_equal(g_ops->run(argv1), HWRUN_OK);
    assert_int_equal(g_ops->history_count(), 0); /* run 不追加历史 */
    assert_true(g_ops->run(NULL) < 0);
    char *argv2[] = {"", NULL};
    assert_true(g_ops->run(argv2) < 0);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_shell_version_status), cmocka_unit_test(test_shell_builtin_help_echo),
        cmocka_unit_test(test_shell_builtin_pwd),    cmocka_unit_test(test_shell_history),
        cmocka_unit_test(test_shell_alias),          cmocka_unit_test(test_shell_external_command),
        cmocka_unit_test(test_shell_error_paths),    cmocka_unit_test(test_shell_run),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}