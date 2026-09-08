/*
 * runtime.c — 运行时注入实现（LOG/PARAM/GIT 回调的转发层）
 *
 * BUS 在 dlopen 插件后、init 前，用 hw_runtime_fill() 构造 hw_runtime_t，
 * 经 hw_plugin_inject_runtime() 投递给插件内部的静态副本。
 * GIT 回调由 loader 在解析到真实 hw_git_ops_t 后补填。
 */

#include "bus.h"
#include "param.h"
#include "log.h"

#include <stdarg.h>

static hw_bus_t *g_bus;   /* 单例：总线只有一个实例 */

/* ---- LOG：转发到 LOG 子系统的 va_list 版本 ---- */
static void rt_log(int level, const char *plugin_id, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (g_bus) hw_log_vwrite(&g_bus->log, level, plugin_id, fmt, ap);
    va_end(ap);
}

/* ---- PARAM：转发到 PARAM 子系统（省略 ctx，由总线单例承载） ---- */
static const char *rt_param_get(const char *key) {
    return g_bus ? hw_param_get(&g_bus->params, key) : NULL;
}
static int rt_param_get_int(const char *key, int def) {
    return g_bus ? hw_param_get_int(&g_bus->params, key, def) : def;
}
static bool rt_param_get_bool(const char *key, bool def) {
    return g_bus ? hw_param_get_bool(&g_bus->params, key, def) : def;
}
static int rt_param_set(const char *key, const char *value, int type,
                        const char *desc) {
    return g_bus ? hw_param_set_value(&g_bus->params, key, value, type, desc)
                 : HWRUN_EINVAL;
}
static int rt_param_watch(const char *plugin_id, const char *pattern,
                          int (*cb)(const char*, const char*, const char*, void*),
                          void *userdata) {
    return g_bus ? hw_param_watch(&g_bus->params, plugin_id, pattern, cb, userdata)
                 : HWRUN_EINVAL;
}

void hw_runtime_bus_bind(hw_bus_t *bus) {
    g_bus = bus;
}

/* 构造 LOG/PARAM 回调；GIT 由 loader 解析到真实 ops 后补填 */
void hw_runtime_fill(hw_runtime_t *rt) {
    memset(rt, 0, sizeof(*rt));
    rt->log            = rt_log;
    rt->param_get      = rt_param_get;
    rt->param_get_int  = rt_param_get_int;
    rt->param_get_bool = rt_param_get_bool;
    rt->param_set      = rt_param_set;
    rt->param_watch    = rt_param_watch;
}

/* 供测试直接访问总线单例 */
hw_bus_t *hw_runtime_bus(void) {
    return g_bus;
}