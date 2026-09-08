/*
 * hwrun_plugin.h — 插件 SDK：统一入口与描述符生成
 *
 * 所有插件必须且只需在入口 .c 里做两件事：
 *   1. 定义本插件的 hw_plugin_ops_t 实例（g_ops），填充生命周期回调；
 *   2. 在文件末尾调用 HWRUN_PLUGIN_DEFINE(...) 生成静态描述符与导出入口。
 *
 * 示例（最小插件入口 .c）：
 *
 *   #include "hwrun.h"
 *   #include "hwrun_plugin.h"
 *
 *   static int xxx_init(hw_plugin_t *s) { ...; return HWRUN_OK; }
 *   static int xxx_start(hw_plugin_t *s) { ...; return HWRUN_OK; }
 *   static void *xxx_get_interface(const char *proto) { ... }
 *
 *   static hw_plugin_ops_t g_ops = {
 *       .init = xxx_init, .start = xxx_start,
 *       .get_interface = xxx_get_interface,
 *   };
 *
 *   // 协议清单：以 NULL 哨兵结尾的只读数组（.so 静态数据）
 *   static const char *const g_provides[] = { "GIT", NULL };
 *   static const char *const g_requires[] = { "LOG", "PARAM", "METAPROTO", NULL };
 *
 *   HWRUN_PLUGIN_BIND()   // 定义运行时槽，仅需一次
 *   HWRUN_PLUGIN_DEFINE(
 *       "git",                     // id（须与 plugin.yml 一致）
 *       "Git 版本控制",            // name
 *       "1.0.0",                   // version
 *       HWPLUGIN_TYPE_GIT,         // type
 *       "系统 Git 命令封装层",      // description
 *       &g_ops,                    // hw_plugin_ops_t* 地址
 *       g_provides, g_requires)
 *
 * 说明：
 *   - entry 由宏导出（visibility default），插件源码不要再手写 hw_plugin_entry。
 *   - 同名 hw_plugin_runtime_bind/get 的 8 份副本刻意保留：BUS 经结构体字段
 *     runtime_bind 精确投递，不受 RTLD_GLOBAL 符号覆盖影响（见 bus/loader.c）。
 *   - 描述符为 static，entry 首次调用时装配（懒初始化），兼容重复 dlopen。
 *   - provides/requires 数组必须是"以 NULL 结尾的只读数组"，宏按哨兵推导计数。
 */

#ifndef HWRUN_PLUGIN_H
#define HWRUN_PLUGIN_H

#include "hwrun.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 插件入口与运行时槽的符号声明（供 bus/loader 与测试 dlsym 前引用） */
extern hw_plugin_t *hw_plugin_entry(void);
extern void hw_plugin_runtime_bind(hw_runtime_t *rt);
extern hw_runtime_t *hw_plugin_runtime_get(void);

/* ============================================================
 * 运行时槽定义（HWRUN_PLUGIN_BIND 展开一次）
 * ============================================================ */
#define HWRUN_PLUGIN_BIND()                                                                        \
    static hw_runtime_t *g_hwrt;                                                                   \
    void hw_plugin_runtime_bind(hw_runtime_t *rt) {                                                \
        g_hwrt = rt;                                                                               \
    }                                                                                              \
    hw_runtime_t *hw_plugin_runtime_get(void) {                                                    \
        return g_hwrt;                                                                             \
    }

/* ============================================================
 * 插件描述符生成（懒初始化）
 *
 *   _id    插件标识（须与 plugin.yml 一致）
 *   _name  显示名
 *   _ver   语义版本
 *   _type  hwplugin_type_t 枚举值
 *   _desc  一行描述
 *   _ops_p hw_plugin_ops_t*（本编译单元 g_ops 的地址）
 *   _prov  provides 数组（const char*[]，NULL 哨兵结尾）
 *   _req   requires 数组（同上）
 * ============================================================ */
#define HWRUN_PLUGIN_DEFINE(_id, _name, _ver, _type, _desc, _ops_p, _prov, _req)                   \
    static hw_plugin_t g_hwplugin;                                                                 \
    __attribute__((visibility("default"))) hw_plugin_t *hw_plugin_entry(void) {                    \
        if (g_hwplugin.id[0] == '\0') {                                                            \
            size_t _i;                                                                             \
            memset(&g_hwplugin, 0, sizeof(g_hwplugin));                                            \
            snprintf(g_hwplugin.id, sizeof(g_hwplugin.id), "%s", (_id));                           \
            snprintf(g_hwplugin.name, sizeof(g_hwplugin.name), "%s", (_name));                     \
            snprintf(g_hwplugin.version, sizeof(g_hwplugin.version), "%s", (_ver));                \
            snprintf(g_hwplugin.description, sizeof(g_hwplugin.description), "%s", (_desc));       \
            g_hwplugin.type = (_type);                                                             \
            g_hwplugin.state = HWPLUGIN_INSTALLED;                                                 \
            g_hwplugin.provides = (char **)(void *)(_prov);                                        \
            for (_i = 0; (_prov)[_i] != NULL; _i++)                                                \
                ;                                                                                  \
            g_hwplugin.provides_count = (int)_i;                                                   \
            g_hwplugin.requires = (char **)(void *)(_req);                                         \
            for (_i = 0; (_req)[_i] != NULL; _i++)                                                 \
                ;                                                                                  \
            g_hwplugin.requires_count = (int)_i;                                                   \
            g_hwplugin.ops = *(_ops_p);                                                            \
            g_hwplugin.runtime_bind = hw_plugin_runtime_bind;                                      \
        }                                                                                          \
        return &g_hwplugin;                                                                        \
    }

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_PLUGIN_H */
