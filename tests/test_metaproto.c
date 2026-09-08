/*
 * test_metaproto.c — CMocka 单元测试：METAPROTO 协议注册表
 *
 * 测试对象：bus/src/metaproto.c（hw_metaproto_* + 版本兼容判断）。
 *
 * 覆盖：
 *   - register / resolve 命中（协议/版本/插件 id/实现指针）；
 *   - 同插件同协议重复 register 走"更新分支"仍 OK（热替换实现与版本）；
 *   - unregister 后 resolve 返回 HWRUN_ENOENT，二次 unregister 亦 ENOENT；
 *   - 版本兼容：版本升到 2.0 后按 "1.0" 解析不再命中、按 "2.0" 命中；
 *   - subscribe：register 触发订阅回调且参数正确（REGISTERED/CHANGED/REMOVED
 *     三种状态），protocol==NULL 的订阅者收全部协议通知；
 *   - check_deps：依赖齐全返回 OK、缺失返回 ENOENT 并回填首个缺失协议。
 *
 * 用例单线程，锁为 no-op 模式即可跑通（不调 hw_locker_enable_parallel）。
 */

#include "hwrun.h"
#include "metaproto.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <cmocka.h>

/* ---- 订阅回调记录（单线程测试，静态缓冲足够） ---- */
static int  g_sub_calls;
static char g_sub_proto[64];
static char g_sub_ver[16];
static char g_sub_plugin[64];
static int  g_sub_state;

static void sub_cb(const char *protocol, const char *version,
                   const char *plugin_id, int state) {
    g_sub_calls++;
    snprintf(g_sub_proto, sizeof(g_sub_proto), "%s", protocol ? protocol : "");
    snprintf(g_sub_ver, sizeof(g_sub_ver), "%s", version ? version : "");
    snprintf(g_sub_plugin, sizeof(g_sub_plugin), "%s",
             plugin_id ? plugin_id : "");
    g_sub_state = state;
}

static void reset_records(void) {
    g_sub_calls = 0;
    g_sub_proto[0] = g_sub_ver[0] = g_sub_plugin[0] = '\0';
    g_sub_state = 0;
}

/* ---- register / resolve / unregister / 版本兼容 ---- */
static void test_register_resolve_unregister(void **state) {
    (void)state;
    hw_metaproto_registry_t reg;
    hw_protocol_route_t *r = NULL;
    hw_protocol_route_t **arr = NULL;
    int cnt = 0;

    assert_int_equal(hw_metaproto_init(&reg, NULL), HWRUN_OK);

    /* 空表与非法入参 */
    assert_int_equal(hw_metaproto_resolve(&reg, "NOPROTO", NULL, &r),
                     HWRUN_ENOENT);
    assert_int_equal(hw_metaproto_resolve(&reg, "X", NULL, NULL),
                     HWRUN_EINVAL);
    assert_int_equal(hw_metaproto_register(&reg, NULL, "1.0", "p", NULL),
                     HWRUN_EINVAL);

    /* 注册 → 解析命中 */
    assert_int_equal(hw_metaproto_register(&reg, "ECHO", "1.0", "plug-a",
                                           (void *)(intptr_t)0x111), HWRUN_OK);
    r = NULL;
    assert_int_equal(hw_metaproto_resolve(&reg, "ECHO", NULL, &r), HWRUN_OK);
    assert_non_null(r);
    assert_string_equal(r->protocol, "ECHO");
    assert_string_equal(r->plugin_id, "plug-a");
    assert_string_equal(r->version, "1.0");
    assert_ptr_equal(r->implementation, (void *)(intptr_t)0x111);

    /* 按版本请求解析：相同版本命中 */
    r = NULL;
    assert_int_equal(hw_metaproto_resolve(&reg, "ECHO", "1.0", &r), HWRUN_OK);
    assert_non_null(r);

    /* 同插件同协议重复注册 → 更新分支（仍 OK），路由数不变 */
    assert_int_equal(hw_metaproto_register(&reg, "ECHO", "2.0", "plug-a",
                                           (void *)(intptr_t)0x222), HWRUN_OK);
    assert_int_equal(hw_metaproto_list(&reg, &arr, &cnt), HWRUN_OK);
    assert_int_equal(cnt, 1);
    free(arr);

    /* 更新后的实现与版本生效 */
    r = NULL;
    assert_int_equal(hw_metaproto_resolve(&reg, "ECHO", NULL, &r), HWRUN_OK);
    assert_non_null(r);
    assert_ptr_equal(r->implementation, (void *)(intptr_t)0x222);
    assert_string_equal(r->version, "2.0");

    /* 版本兼容：旧请求 1.0 不再命中，2.0 命中 */
    r = NULL;
    assert_int_equal(hw_metaproto_resolve(&reg, "ECHO", "1.0", &r),
                     HWRUN_ENOENT);
    assert_int_equal(hw_metaproto_resolve(&reg, "ECHO", "2.0", &r), HWRUN_OK);
    assert_non_null(r);

    /* 同协议不同插件：允许并存（新增路由） */
    assert_int_equal(hw_metaproto_register(&reg, "ECHO", "1.0", "plug-b",
                                           (void *)(intptr_t)0x333), HWRUN_OK);
    assert_int_equal(hw_metaproto_list(&reg, &arr, &cnt), HWRUN_OK);
    assert_int_equal(cnt, 2);
    free(arr);

    /* 注销 plug-b 后：仍可解析到 plug-a */
    assert_int_equal(hw_metaproto_unregister(&reg, "ECHO", "plug-b"),
                     HWRUN_OK);
    r = NULL;
    assert_int_equal(hw_metaproto_resolve(&reg, "ECHO", NULL, &r), HWRUN_OK);
    assert_non_null(r);
    assert_string_equal(r->plugin_id, "plug-a");

    /* 全部注销后 ENOENT；二次注销 ENOENT */
    assert_int_equal(hw_metaproto_unregister(&reg, "ECHO", "plug-a"),
                     HWRUN_OK);
    assert_int_equal(hw_metaproto_resolve(&reg, "ECHO", NULL, &r),
                     HWRUN_ENOENT);
    assert_int_equal(hw_metaproto_unregister(&reg, "ECHO", "plug-a"),
                     HWRUN_ENOENT);

    hw_metaproto_shutdown(&reg);
}

/* ---- subscribe 通知：register 触发回调、状态与参数正确 ---- */
static void test_subscribe_notify(void **state) {
    (void)state;
    hw_metaproto_registry_t reg;

    assert_int_equal(hw_metaproto_init(&reg, NULL), HWRUN_OK);
    reset_records();

    /* 非法入参 */
    assert_int_equal(hw_metaproto_subscribe(&reg, NULL, "ECHO", sub_cb),
                     HWRUN_EINVAL);
    assert_int_equal(hw_metaproto_subscribe(&reg, "w", "ECHO", NULL),
                     HWRUN_EINVAL);

    /* 订阅 ECHO 协议；重复订阅同 plugin+protocol → 更新回调仍 OK */
    assert_int_equal(hw_metaproto_subscribe(&reg, "watcher", "ECHO", sub_cb),
                     HWRUN_OK);
    assert_int_equal(hw_metaproto_subscribe(&reg, "watcher", "ECHO", sub_cb),
                     HWRUN_OK);

    /* register → 订阅回调（state=REGISTERED），参数逐项正确 */
    assert_int_equal(hw_metaproto_register(&reg, "ECHO", "1.0", "plug-a",
                                           (void *)(intptr_t)1), HWRUN_OK);
    assert_int_equal(g_sub_calls, 1);
    assert_string_equal(g_sub_proto, "ECHO");
    assert_string_equal(g_sub_ver, "1.0");
    assert_string_equal(g_sub_plugin, "plug-a");
    assert_int_equal(g_sub_state, HWPROTO_STATE_REGISTERED);

    /* 更新注册 → state=CHANGED，带新版本 */
    assert_int_equal(hw_metaproto_register(&reg, "ECHO", "1.1", "plug-a",
                                           (void *)(intptr_t)2), HWRUN_OK);
    assert_int_equal(g_sub_calls, 2);
    assert_string_equal(g_sub_ver, "1.1");
    assert_int_equal(g_sub_state, HWPROTO_STATE_CHANGED);

    /* 注销 → state=REMOVED */
    assert_int_equal(hw_metaproto_unregister(&reg, "ECHO", "plug-a"),
                     HWRUN_OK);
    assert_int_equal(g_sub_calls, 3);
    assert_int_equal(g_sub_state, HWPROTO_STATE_REMOVED);
    assert_string_equal(g_sub_plugin, "plug-a");

    /* protocol==NULL 的订阅者收全部协议；带协议过滤的订阅者被过滤 */
    assert_int_equal(hw_metaproto_subscribe(&reg, "watcher-all", NULL, sub_cb),
                     HWRUN_OK);
    assert_int_equal(hw_metaproto_register(&reg, "OTHER", "1.0", "plug-b",
                                           (void *)(intptr_t)3), HWRUN_OK);
    assert_int_equal(g_sub_calls, 4);
    assert_string_equal(g_sub_proto, "OTHER");
    assert_string_equal(g_sub_plugin, "plug-b");
    assert_int_equal(g_sub_state, HWPROTO_STATE_REGISTERED);

    hw_metaproto_shutdown(&reg);
}

/* ---- check_deps：依赖齐全 / 缺失 ---- */
static void test_check_deps(void **state) {
    (void)state;
    hw_metaproto_registry_t reg;

    assert_int_equal(hw_metaproto_init(&reg, NULL), HWRUN_OK);
    assert_int_equal(hw_metaproto_register(&reg, "LOG", "1.0", "plug-log",
                                           NULL), HWRUN_OK);
    assert_int_equal(hw_metaproto_register(&reg, "PARAM", "1.0", "plug-param",
                                           NULL), HWRUN_OK);

    /* 齐全 */
    const char *need_ok[] = { "LOG", "PARAM" };
    assert_int_equal(hw_metaproto_check_deps(&reg, need_ok, 2, NULL, 0),
                     HWRUN_OK);

    /* 缺失：ENOENT 且回填首个缺失协议 */
    const char *need_miss[] = { "LOG", "GIT" };
    char missing[64] = "";
    assert_int_equal(hw_metaproto_check_deps(&reg, need_miss, 2,
                                             missing, sizeof(missing)),
                     HWRUN_ENOENT);
    assert_string_equal(missing, "GIT");

    /* 空依赖集（count=0）→ OK */
    const char *need_none[] = { "LOG" };
    assert_int_equal(hw_metaproto_check_deps(&reg, need_none, 0, NULL, 0),
                     HWRUN_OK);

    hw_metaproto_shutdown(&reg);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_register_resolve_unregister),
        cmocka_unit_test(test_subscribe_notify),
        cmocka_unit_test(test_check_deps),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
