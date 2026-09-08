/*
 * sp_selftest.c — SP 插件 dlopen 自测
 *
 * 直接 dlopen build/sp.so，调用 hw_plugin_entry()，执行 init/start，
 * 通过 get_interface("SP") 取 hw_sp_ops_t，运行 sp_selftest() 并报告结果。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "../include/hwrun.h"
#include "../include/sp.h"

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "./build/sp.so";
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "[sp-selftest] dlopen(%s) failed: %s\n", path, dlerror());
        return 1;
    }

    hw_plugin_t *(*entry)(void) = (hw_plugin_t * (*)(void)) dlsym(h, "hw_plugin_entry");
    if (!entry) {
        fprintf(stderr, "[sp-selftest] dlsym hw_plugin_entry failed: %s\n", dlerror());
        dlclose(h);
        return 1;
    }

    hw_plugin_t *p = entry();
    printf("[sp-selftest] plugin id=%s name=%s version=%s type=%d\n", p->id, p->name, p->version,
           (int)p->type);
    printf("[sp-selftest] provides=%d requires=%d\n", p->provides_count, p->requires_count);

    if (p->ops.init(p) != HWRUN_OK) {
        fprintf(stderr, "[sp-selftest] init FAILED\n");
        dlclose(h);
        return 2;
    }
    if (p->ops.start(p) != HWRUN_OK) {
        fprintf(stderr, "[sp-selftest] start FAILED\n");
        dlclose(h);
        return 2;
    }

    hw_sp_ops_t *ops = (hw_sp_ops_t *)p->ops.get_interface("SP");
    if (!ops) {
        fprintf(stderr, "[sp-selftest] get_interface(\"SP\") returned NULL\n");
        dlclose(h);
        return 3;
    }
    printf("[sp-selftest] got hw_sp_ops_t via get_interface(\"SP\")\n");

    int rc = ops->selftest();
    printf("[sp-selftest] sp_selftest() => %s (rc=%d)\n", rc == 0 ? "PASS" : "FAIL", rc);

    /* 触发一次 stop/destroy 验证生命周期可清理 */
    p->ops.stop(p);
    p->ops.destroy(p);
    dlclose(h);

    return rc == 0 ? 0 : 4;
}