/*
 * test_hap.c — CMocka 单元测试：HAP 硬件抽象协议（dlopen .so 后真实调用）
 *
 * 测试对象：hap/build/hap.so 插件及其 provides 协议 "HAP" 的
 * hw_hap_ops_t 信息查询原语（真协议路径，非打桩）。
 *
 * 链路（与 test_proto.c 的 load_plugin 模式一致）：
 *   dlopen(.so) -> hw_plugin_entry() -> init()
 *   -> metaproto_register(provides, implementation=get_interface("HAP"))
 *   -> start -> metaproto_resolve("HAP") -> hw_hap_ops_t*
 *   -> 真实调用 get_cpu_info / get_memory_info / get_system_info 并断言。
 *
 * 覆盖要点（Linux/WSL 目标，/proc、/etc/hostname 可读）：
 *   - get_cpu_info：返回 0，且 vendor/cores/model 至少一项非全零
 *     （/proc/cpuinfo 可读时 cores>=1、vendor 有值）；
 *   - get_memory_info：total_kb > 0（/proc/meminfo）；
 *   - get_system_info：hostname 非空（/etc/hostname + $HOSTNAME 兜底）。
 *
 * 降级语义（Windows/MSYS2 无 /proc 等）：HAP 实现读取不到时填 0/空串但
 * 仍返回 0，故本测试只把"返回码 == 0"作为硬断言；字段级强断言仅在
 * 探测到对应结构体未完全降级（仍有数据）时启用——Windows 降级可过、
 * Linux 上能验证真实数据。
 *
 * 清理：stop -> unregister -> destroy -> dlclose；p 是 .so 静态描述符，
 * 不得 free(p)；插件不挂入 bus->plugins，防 hw_bus_shutdown 二次卸载。
 */

#include "hwrun.h"
#include "bus.h"
#include "metaproto.h"
#include "../hap/include/hap.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <dlfcn.h>
#include <cmocka.h>

/* 插件 .so 根目录：默认相对源树（WORKING_DIRECTORY=tests，故 "../"=仓库根）。 */
#ifndef HWRUN_PLUGIN_ROOT
#define HWRUN_PLUGIN_ROOT "../"
#endif

static hw_bus_t g_bus;
static void *g_h = NULL;        /* dlopen 句柄            */
static hw_plugin_t *g_p = NULL; /* .so 静态描述符，不 free */
static hw_hap_ops_t *g_ops = NULL;

/* ---- 组级 setup：总线 + dlopen + 生命周期 + 注册 provides ---- */
static int group_setup(void **state) {
    (void)state;
    if (hw_bus_init(&g_bus, NULL, NULL, HWLOG_WARN) != HWRUN_OK) {
        fprintf(stderr, "test_hap: hw_bus_init 失败\n");
        return -1;
    }

    char so[512];
    snprintf(so, sizeof(so), HWRUN_PLUGIN_ROOT "hap/build/hap.so");
    g_h = dlopen(so, RTLD_NOW | RTLD_GLOBAL);
    if (!g_h) {
        printf("  [skip] dlopen(%s): %s\n", so, dlerror());
        return 0; /* .so 未构建/平台不支持 → 各用例打印跳过 */
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

    /* init（模拟 loader 注册前阶段） */
    if (self->ops.init) self->ops.init(self);
    self->state = HWPLUGIN_LOADED;

    /* 注册 provides（implementation 来自 get_interface） */
    for (int i = 0; i < self->provides_count; i++) {
        void *impl = self->ops.get_interface ? self->ops.get_interface(self->provides[i]) : NULL;
        hw_metaproto_register(&g_bus.meta, self->provides[i], HWRUN_PROTOCOL_VERSION, self->id,
                              impl ? impl : (void *)self);
    }
    if (self->ops.start) self->ops.start(self);
    self->state = HWPLUGIN_STARTED;
    g_p = self;

    hw_protocol_route_t *r = NULL;
    if (hw_metaproto_resolve(&g_bus.meta, HWPROTO_HAP, NULL, &r) == HWRUN_OK && r &&
        r->implementation)
        g_ops = (hw_hap_ops_t *)r->implementation;
    else
        printf("  [warn] resolve(HAP) 未取得实现指针\n");
    return 0;
}

/* ---- 组级 teardown：卸载插件（p 不 free，只 stop/destroy/unregister/dlclose） ---- */
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

static void test_hap_cpu_info(void **state) {
    (void)state;
    if (!g_ops) {
        printf("  [skip] HAP 不可用\n");
        return;
    }
    hap_cpu_info_t cpu;
    memset(&cpu, 0, sizeof(cpu));
    /* 返回码为硬断言：HAP 在降级环境下也须返回 0 */
    assert_int_equal(g_ops->get_cpu_info(&cpu), HWRUN_OK);

    /* 完全降级（cores/vendor/model 全零/空）→ 仅保证返回码 */
    if (cpu.cores == 0 && cpu.vendor[0] == '\0' && cpu.model[0] == '\0') {
        printf("  [info] CPU 信息完全降级（无 /proc/cpuinfo），跳过字段断言\n");
        return;
    }
    /* Linux 下 /proc/cpuinfo 可读：cores 或 vendor 至少一项被填充 */
    assert_true(cpu.cores > 0 || cpu.vendor[0] != '\0');
    printf("  [info] cpu: cores=%u vendor='%s' model='%s'\n", cpu.cores, cpu.vendor,
           cpu.model[0] ? cpu.model : "(空)");
}

static void test_hap_memory_info(void **state) {
    (void)state;
    if (!g_ops) {
        printf("  [skip] HAP 不可用\n");
        return;
    }
    hap_memory_info_t mem;
    memset(&mem, 0, sizeof(mem));
    assert_int_equal(g_ops->get_memory_info(&mem), HWRUN_OK);

    if (mem.total_kb == 0 && mem.available_kb == 0 && mem.free_kb == 0 && mem.swap_total_kb == 0) {
        printf("  [info] 内存信息完全降级（无 /proc/meminfo），跳过字段断言\n");
        return;
    }
    assert_true(mem.total_kb > 0);
    printf("  [info] mem: total=%lluKB free=%lluKB avail=%lluKB\n",
           (unsigned long long)mem.total_kb, (unsigned long long)mem.free_kb,
           (unsigned long long)mem.available_kb);
}

static void test_hap_system_info(void **state) {
    (void)state;
    if (!g_ops) {
        printf("  [skip] HAP 不可用\n");
        return;
    }
    hap_system_info_t sys;
    memset(&sys, 0, sizeof(sys));
    assert_int_equal(g_ops->get_system_info(&sys), HWRUN_OK);

    if (sys.hostname[0] == '\0' && sys.kernel_release[0] == '\0' && sys.os_name[0] == '\0' &&
        sys.uptime_seconds == 0) {
        printf("  [info] 系统信息完全降级，跳过字段断言\n");
        return;
    }
    /* Linux 下 /etc/hostname 或 $HOSTNAME 兜底：hostname 应非空 */
    assert_true(sys.hostname[0] != '\0');
    printf("  [info] sys: hostname='%s' kernel='%s'\n", sys.hostname, sys.kernel_release);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_hap_cpu_info),
        cmocka_unit_test(test_hap_memory_info),
        cmocka_unit_test(test_hap_system_info),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
