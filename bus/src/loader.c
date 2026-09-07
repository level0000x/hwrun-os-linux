/*
 * loader.c — 插件加载器
 *
 * 用 dlopen 加载插件 .so，绑定生命周期函数，注册协议到 METAPROTO。
 */

#include "bus.h"

#include <dlfcn.h>
#include <time.h>

#if defined(_WIN32)
/* MSYS2 下以 dlopen 兼容 POSIX；这里统一用 dlfcn.h */
#endif

extern int hw_plugin_load_so(hw_bus_t *bus, hw_plugin_t *p);
extern int hw_plugin_start(hw_bus_t *bus, hw_plugin_t *p);

/* 每个插件 .so 必须导出的入口符号 */
typedef struct hw_plugin *(*plugin_entry_fn)(void);

/* 统一的"C 接口"符号：hw_plugin_entry() */
typedef struct hw_plugin *(*hw_plugin_entry_v1)(void);

int hw_plugin_load_so(hw_bus_t *bus, hw_plugin_t *p) {
    if (!bus || !p) return HWRUN_EINVAL;
    if (p->handle) return HWRUN_OK;   /* 已加载 */

    /* 找出第一个文件路径（.so）作为加载目标 */
    if (!p->files || p->files_count == 0) return HWRUN_ENOENT;
    const char *path = NULL;
    for (int i = 0; i < p->files_count; i++) {
        if (p->files[i] && strstr(p->files[i], ".so")) { path = p->files[i]; break; }
    }
    if (!path) return HWRUN_ENOENT;

    void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        fprintf(stderr, "HWRun LOADER: dlopen(%s) failed: %s\n", path, dlerror());
        return HWRUN_ENOENT;
    }

    /* 取入口 */
    hw_plugin_entry_v1 entry = (hw_plugin_entry_v1)dlsym(h, "hw_plugin_entry");
    if (!entry) {
        dlclose(h);
        fprintf(stderr, "HWRun LOADER: %s missing hw_plugin_entry()\n", path);
        return HWRUN_EILSEQ;
    }

    hw_plugin_t *self = entry();
    if (!self) { dlclose(h); return HWRUN_EILSEQ; }

    /* 以插件声明的 id 为准，校验一致性 */
    if (p->id[0] && !hw_str_eq(p->id, self->id)) {
        HWLOG_WARNF(&bus->log, "loader",
                    "id mismatch: declared=%s actual=%s", p->id, self->id);
    }
    /* 合并：采用 .so 内的完整描述，但保留声明路径 */
    if (!self->files) {
        self->files = p->files;
        self->files_count = p->files_count;
    }
    p->handle = h;
    p->ops = self->ops;
    p->load_time = (uint64_t)time(NULL);

    /* 提供给插件一个途径取得总线相关句柄（可选） */
    (void)bus;
    return HWRUN_OK;
}

int hw_plugin_start(hw_bus_t *bus, hw_plugin_t *p) {
    if (!bus || !p) return HWRUN_EINVAL;

    hw_protocol_route_t *route = NULL;
    int rc = hw_metaproto_check_deps(&bus->meta,
                                     (const char *const *)p->requires,
                                     p->requires_count, NULL, 0);
    if (rc != HWRUN_OK) {
        char miss[64] = "";
        hw_metaproto_check_deps(&bus->meta,
                                (const char *const *)p->requires,
                                p->requires_count, miss, sizeof(miss));
        HWLOG_ERRF(&bus->log, p->id, "missing dependency: %s", miss);
        p->state = HWPLUGIN_ERROR;
        return HWRUN_EILSEQ;
    }

    /* init */
    if (p->ops.init) {
        rc = p->ops.init(p);
        if (rc != HWRUN_OK) { p->state = HWPLUGIN_ERROR; return rc; }
    }
    p->state = HWPLUGIN_LOADED;

    /* 注册插件提供的协议 */
    for (int i = 0; i < p->provides_count; i++) {
        void *impl = p->ops.get_interface ? p->ops.get_interface(p->provides[i]) : NULL;
        hw_metaproto_register(&bus->meta, p->provides[i], HWRUN_PROTOCOL_VERSION,
                              p->id, impl ? impl : (void *)p);
    }

    /* start */
    if (p->ops.start) {
        rc = p->ops.start(p);
        if (rc != HWRUN_OK) { p->state = HWPLUGIN_ERROR; return rc; }
    }
    p->state = HWPLUGIN_STARTED;
    HWLOG_INFOF(&bus->log, "bus", "plugin started: %s v%s", p->id, p->version);
    (void)route;
    return HWRUN_OK;
}

int hw_plugin_stop(hw_bus_t *bus, hw_plugin_t *p) {
    if (!bus || !p) return HWRUN_EINVAL;
    if (p->ops.stop) p->ops.stop(p);
    /* 注销协议 */
    for (int i = 0; i < p->provides_count; i++) {
        hw_metaproto_unregister(&bus->meta, p->provides[i], p->id);
    }
    p->state = HWPLUGIN_STOPPED;
    return HWRUN_OK;
}

int hw_plugin_unload(hw_bus_t *bus, hw_plugin_t *p) {
    if (!bus || !p) return HWRUN_EINVAL;
    if (p->state == HWPLUGIN_STARTED || p->state == HWPLUGIN_LOADED)
        hw_plugin_stop(bus, p);
    if (p->ops.destroy) p->ops.destroy(p);
    if (p->handle) { dlclose(p->handle); p->handle = NULL; }
    p->state = HWPLUGIN_UNINSTALLED;
    return HWRUN_OK;
}