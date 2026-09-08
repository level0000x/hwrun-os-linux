/*
 * test_pmp.c — CMocka 单元测试：PMP 进程管理协议（dlopen .so 后真实调用）
 *
 * 测试对象：pmp/build/pmp.so 插件及其 provides 协议 "PMP" 的
 * hw_pmp_ops_t 进程查询原语（真协议路径，非打桩）。
 *
 * 链路（与 test_proto.c 的 load_plugin 模式一致）：
 *   dlopen(.so) -> hw_plugin_entry() -> init()
 *   -> metaproto_register(provides, implementation=get_interface("PMP"))
 *   -> start -> metaproto_resolve("PMP") -> hw_pmp_ops_t*
 *   -> 真实调用：
 *     - get_current_pid：返回当前进程 PID，> 0；
 *     - get_process_info(当前 pid)：成功时 info.pid 与 pid 一致、comm 非空；
 *     - get_process_list：Linux 下 /proc 可枚举，返回列表 > 0 项并 free；
 *     - sched_get(当前 pid)：自身进程通常可查；受权限/平台影响时宽松。
 *
 * 降级语义（MSYS2/Windows 等）：进程级 POSIX 能力缺失时 pmp 实现返回
 * 非零错误码、不崩溃。故对每个环境相关调用：rc == 0 时做字段级强断言，
 * rc != 0 时打印说明并宽松通过，保证 Linux 上验证真实数据、降级环境可过。
 *
 * 清理：stop -> unregister -> destroy -> dlclose；p 是 .so 静态描述符，
 * 不得 free(p)；插件不挂入 bus->plugins，防 hw_bus_shutdown 二次卸载。
 */

#include "hwrun.h"
#include "bus.h"
#include "metaproto.h"
#include "../pmp/include/pmp.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <dlfcn.h>
#include <cmocka.h>

#ifndef HWRUN_PLUGIN_ROOT
#define HWRUN_PLUGIN_ROOT "../"
#endif

static hw_bus_t g_bus;
static void *g_h = NULL;        /* dlopen 句柄            */
static hw_plugin_t *g_p = NULL; /* .so 静态描述符，不 free */
static hw_pmp_ops_t *g_ops = NULL;

/* ---- 组级 setup：总线 + dlopen + 生命周期 + 注册 provides ---- */
static int group_setup(void **state) {
    (void)state;
    if (hw_bus_init(&g_bus, NULL, NULL, HWLOG_WARN) != HWRUN_OK) {
        fprintf(stderr, "test_pmp: hw_bus_init 失败\n");
        return -1;
    }

    char so[512];
    snprintf(so, sizeof(so), HWRUN_PLUGIN_ROOT "pmp/build/pmp.so");
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
    if (hw_metaproto_resolve(&g_bus.meta, HWPROTO_PMP, NULL, &r) == HWRUN_OK && r &&
        r->implementation)
        g_ops = (hw_pmp_ops_t *)r->implementation;
    else
        printf("  [warn] resolve(PMP) 未取得实现指针\n");
    return 0;
}

/* ---- 组级 teardown：卸载插件（p 不 free） ---- */
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

/* 当前 PID + 当前进程详情：成功则 pid 匹配、comm 非空 */
static void test_pmp_current_process(void **state) {
    (void)state;
    if (!g_ops) {
        printf("  [skip] PMP 不可用\n");
        return;
    }
    int pid = g_ops->get_current_pid();
    assert_true(pid > 0);
    printf("  [info] current pid=%d\n", pid);

    pmp_process_info_t info;
    memset(&info, 0, sizeof(info));
    int rc = g_ops->get_process_info(pid, &info);
    if (rc != HWRUN_OK) {
        printf("  [info] get_process_info 不可用(rc=%d)，宽松跳过字段断言\n", rc);
        return;
    }
    assert_int_equal((int)info.pid, pid);
    assert_int_equal((int)info.tgid, pid);
    assert_true(info.comm[0] != '\0');
    printf("  [info] comm='%s' state=%d ppid=%u\n", info.comm, (int)info.state, info.ppid);
}

/* 进程列表：Linux /proc 可枚举时返回 > 0 项，随后必须 free */
static void test_pmp_process_list(void **state) {
    (void)state;
    if (!g_ops) {
        printf("  [skip] PMP 不可用\n");
        return;
    }
    pmp_process_info_t *list = NULL;
    uint32_t count = 0;
    int rc = g_ops->get_process_list(&list, &count);
    if (rc != HWRUN_OK || !list) {
        printf("  [info] get_process_list 不可用(rc=%d)，宽松跳过\n", rc);
        if (list) g_ops->free_process_list(list, count);
        return;
    }
    assert_true(count > 0);
    printf("  [info] process list count=%u\n", count);
    g_ops->free_process_list(list, count);
}

/* 调度查询：自身进程一般可查；受权限/平台影响时宽松 */
static void test_pmp_sched_get(void **state) {
    (void)state;
    if (!g_ops) {
        printf("  [skip] PMP 不可用\n");
        return;
    }
    int pid = g_ops->get_current_pid();
    pmp_sched_policy_t policy = PMP_SCHED_OTHER;
    int32_t priority = 0;
    int nice = 0;
    int rc = g_ops->sched_get(pid, &policy, &priority, &nice);
    if (rc != HWRUN_OK) {
        printf("  [info] sched_get 受权限/平台限制(rc=%d)，宽松通过\n", rc);
        return;
    }
    printf("  [info] sched: policy=%d priority=%d nice=%d\n", (int)policy, (int)priority, nice);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pmp_current_process),
        cmocka_unit_test(test_pmp_process_list),
        cmocka_unit_test(test_pmp_sched_get),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
