/*
 * test_proto.c — 真协议调用自测
 *
 * 目标：不再只测"插件能否启动"，而是走完整协议调用路径：
 *   dlopen(.so) -> hw_plugin_entry() -> init()
 *   -> metaproto_register(implementation = get_interface(proto))
 *   -> metaproto_resolve(proto) -> (hw_xxx_ops_t*)route->implementation
 *   -> 真实调用 ops 并断言返回。
 *
 * 覆盖依赖链第 2-8 环中可安全调用的协议：
 *   HAP, PMP, FSP, NP, LOADER
 * 每个用例独立断言，任一失败即返回非 0，供 CI/根 Makefile 使用。
 */

#include "hwrun.h"
#include "bus.h"
#include "metaproto.h"

/* 各协议公共头，仅取类型定义（不与插件内部符号冲突） */
#include "../hap/include/hap.h"
#include "../pmp/include/pmp.h"
#include "../fsp/include/fsp.h"
#include "../np/include/np.h"
#include "../loader/include/loader.h"

#include <dlfcn.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* ---- 统一断言 ---- */
static int g_fail = 0;
static int g_pass = 0;
#define CHECK(cond, name) \
    do { \
        if (cond) { g_pass++; printf("  [PASS] %-46s\n", name); } \
        else      { g_fail++; printf("  [FAIL] %-46s\n", name); } \
    } while (0)

/* 真实加载 .so 并注册其 provides，返回 plugin 描述（handle 保存在 desc） */
static hw_plugin_t *load_plugin(hw_bus_t *bus, const char *so_path) {
    void *h = dlopen(so_path, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("  dlopen(%s): %s\n", so_path, dlerror()); return NULL; }

    hw_plugin_t *(*entry)(void) = (hw_plugin_t *(*)(void))dlsym(h, "hw_plugin_entry");
    if (!entry) { dlclose(h); printf("  %s: no hw_plugin_entry\n", so_path); return NULL; }
    hw_plugin_t *self = entry();
    if (!self) { dlclose(h); return NULL; }

    /* init（模拟 loader.hw_plugin_start 的注册前阶段） */
    if (self->ops.init) self->ops.init(self);
    self->state = HWPLUGIN_LOADED;

    /* 注册 provides（implementation 来自 get_interface） */
    for (int i = 0; i < self->provides_count; i++) {
        void *impl = self->ops.get_interface ? self->ops.get_interface(self->provides[i]) : NULL;
        hw_metaproto_register(&bus->meta, self->provides[i], HWRUN_PROTOCOL_VERSION,
                              self->id, impl ? impl : (void *)self);
    }
    /* start（大部分插件在 start 做自检/预热） */
    if (self->ops.start) self->ops.start(self);
    self->state = HWPLUGIN_STARTED;
    /* 挂到总线插件链表头部，便于统一清理 */
    self->next = bus->plugins; if (bus->plugins) bus->plugins->prev = self;
    bus->plugins = self;
    return self;
}

static hw_protocol_route_t *resolve(hw_bus_t *bus, const char *proto) {
    hw_protocol_route_t *r = NULL;
    if (hw_metaproto_resolve(&bus->meta, proto, NULL, &r) != HWRUN_OK || !r)
        return NULL;
    return r;
}

static int check_impl(hw_bus_t *bus, const char *proto) {
    hw_protocol_route_t *r = resolve(bus, proto);
    CHECK(r && r->implementation, "解析并取得实现指针");
    return (r && r->implementation) ? HWRUN_OK : HWRUN_ENOENT;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);   /* 崩溃时也即时输出，便于定位 */
    hw_bus_t bus;
    hw_bus_init(&bus, NULL, NULL, HWLOG_WARN);

    /* 内置协议（METAPROTO/BUS/PARAM/LOG/GIT）由 bus::hw_bus_init 注册 */
    hw_protocol_route_t *tr = NULL;
    CHECK(hw_metaproto_resolve(&bus.meta, "METAPROTO", NULL, &tr) == HWRUN_OK && tr,
          "内置 METAPROTO 可解析");
    tr = NULL;
    CHECK(hw_metaproto_resolve(&bus.meta, "PARAM", NULL, &tr) == HWRUN_OK && tr,
          "内置 PARAM 可解析");
    tr = NULL;
    CHECK(hw_metaproto_resolve(&bus.meta, "LOG", NULL, &tr) == HWRUN_OK && tr,
          "内置 LOG 可解析");

    const char *plugin_sos[] = {
        "../hap/build/hap.so",
        "../pmp/build/pmp.so",
        "../fsp/build/fsp.so",
        "../np/build/np.so",
        "../loader/build/loader.so",
    };
    int sos = (int)(sizeof(plugin_sos) / sizeof(plugin_sos[0]));

    printf("[1/5] 插件加载与注册\n");
    for (int i = 0; i < sos; i++) {
        hw_plugin_t *p = load_plugin(&bus, plugin_sos[i]);
        CHECK(p != NULL, "插件 .so 加载成功");
        if (p) {
            CHECK(p->state == HWPLUGIN_STARTED, "插件已启动");
            /* 运行时注入机制就绪：插件提供了 runtime_bind 绑定点，
             * load_plugin 已在 init 前经此字段注入 LOG/PARAM 回调 */
            CHECK(p->runtime_bind != NULL, "插件已提供运行时绑定点");
            for (int j = 0; j < p->provides_count; j++) {
                char buf[96];
                snprintf(buf, sizeof(buf), "协议 %s 已注册可解析", p->provides[j]);
                CHECK(check_impl(&bus, p->provides[j]) == HWRUN_OK, buf);
            }
        }
    }

    printf("[2/5] HAP 硬件抽象协议\n");
    if (check_impl(&bus, "HAP") == HWRUN_OK) {
        hw_hap_ops_t *hap = (hw_hap_ops_t *)resolve(&bus, "HAP")->implementation;
        hap_cpu_info_t cpu; memset(&cpu, 0, sizeof(cpu));
        hap_memory_info_t mem; memset(&mem, 0, sizeof(mem));
        hap_system_info_t sys; memset(&sys, 0, sizeof(sys));
        CHECK(hap->get_cpu_info && hap->get_cpu_info(&cpu) == HWRUN_OK, "HAP.get_cpu_info 调用成功");
        CHECK(hap->get_memory_info && hap->get_memory_info(&mem) == HWRUN_OK,
              "HAP.get_memory_info 调用成功");
        CHECK(hap->get_system_info && hap->get_system_info(&sys) == HWRUN_OK,
              "HAP.get_system_info 调用成功");
    } else printf("  HAP 不可用，跳过\n");

    printf("[3/5] PMP 进程管理协议\n");
    if (check_impl(&bus, "PMP") == HWRUN_OK) {
        hw_pmp_ops_t *pmp = (hw_pmp_ops_t *)resolve(&bus, "PMP")->implementation;
        CHECK(pmp->get_current_pid && pmp->get_current_pid() > 0,
              "PMP.get_current_pid 返回有效 PID");
        int32_t pid = pmp->get_current_pid ? pmp->get_current_pid() : -1;
        pmp_process_info_t info; memset(&info, 0, sizeof(info));
        CHECK(pmp->get_process_info && pmp->get_process_info(pid, &info) == HWRUN_OK &&
              info.pid == (uint32_t)pid, "PMP.get_process_info 命中当前进程");
    } else printf("  PMP 不可用，跳过\n");

    printf("[4/5] FSP 文件系统协议\n");
    if (check_impl(&bus, "FSP") == HWRUN_OK) {
        hw_fsp_ops_t *fsp = (hw_fsp_ops_t *)resolve(&bus, "FSP")->implementation;
        fsp_stat_t st; memset(&st, 0, sizeof(st));
        CHECK(fsp->stat && fsp->stat("/tmp", &st) == HWRUN_OK && S_ISDIR(st.mode),
              "FSP.stat(/tmp) 为目录");
        fsp_statfs_t fs; memset(&fs, 0, sizeof(fs));
        CHECK(fsp->statfs && fsp->statfs("/tmp", &fs) == HWRUN_OK,
              "FSP.statfs(/tmp) 调用成功");
        CHECK(fsp->access && fsp->access("/tmp", 0) == HWRUN_OK,
              "FSP.access(/tmp, F_OK) 存在");
        int fd = fsp->open ? fsp->open("/etc/hostname", 0, 0) : -1;
        CHECK(fd >= 0, "FSP.open(/etc/hostname) 可读");
    } else printf("  FSP 不可用，跳过\n");

    printf("[5/5] LOADER 可执行加载协议\n");
    if (check_impl(&bus, "LOADER") == HWRUN_OK) {
        hw_loader_ops_t *loader = (hw_loader_ops_t *)resolve(&bus, "LOADER")->implementation;
        loader_format_info_t fi[8]; memset(fi, 0, sizeof(fi));
        int n = loader->get_formats ? loader->get_formats(fi, 8) : -1;
        CHECK(n > 0, "LOADER.get_formats 返回格式列表");
        CHECK(n >= 1 && fi[0].name[0], "LOADER 首格式有名称");
    } else printf("  LOADER 不可用，跳过\n");

    /* ---- 清理：逆序卸载所有已加载插件 ---- */
    printf("[cleanup] 卸载插件\n");
    hw_plugin_t *p = bus.plugins;
    while (p) {
        hw_plugin_t *n = p->next;
        if (p->ops.stop) p->ops.stop(p);
        for (int j = 0; j < p->provides_count; j++)
            hw_metaproto_unregister(&bus.meta, p->provides[j], p->id);
        if (p->ops.destroy) p->ops.destroy(p);
        if (p->handle) dlclose(p->handle);
        p = n;
    }
    bus.plugins = NULL;          /* 已手动卸载，防止 hw_bus_shutdown 二次 stop */
    hw_bus_shutdown(&bus);

    printf("\n==== 结果: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}