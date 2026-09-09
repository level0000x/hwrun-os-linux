/*
 * hwrun.h — HWRun OS 核心契约
 *
 * 所有插件、协议、参数、日志子系统共享的类型与接口定义。
 * 是整个依赖链最底层的基础，一切模块依赖本头文件。
 *
 * 设计哲学：一切皆插件，一切皆参数。
 */

#ifndef HWRUN_H
#define HWRUN_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 版本
 * ============================================================ */
#define HWRUN_VERSION "7.0.0"
#define HWRUN_PROTOCOL_VERSION "1.0"

/* ============================================================
 * 错误码 —— 全库负 errno 化
 *
 * 约定：
 *   成功返回 0（HWRUN_OK）。
 *   失败一律返回负 errno（-EINVAL/-ENOENT/...），与 libc 语义一致，
 *   可直接用 strerror(-rc) 转描述。
 *   无标准 errno 对应的 HWRun 私有错误落在 HW_EBASE 私有域。
 *
 * 旧版 HWRUN_E* 为离散负小值（-1..-11），与 glibc errno 数值冲突，
 * 现收敛为负 errno 别名，源码继续使用 HWRUN_E* 亦语义正确。
 * ============================================================ */
#include <errno.h>

#define HWRUN_OK 0
#define HWRUN_EPERM (-EPERM)
#define HWRUN_ENOENT (-ENOENT)
#define HWRUN_ENOMEM (-ENOMEM)
#define HWRUN_EEXIST (-EEXIST)
#define HWRUN_ENOTSUP (-ENOTSUP)
#define HWRUN_EILSEQ (-EILSEQ) /* 协议不匹配 */
#define HWRUN_EAGAIN (-EAGAIN)
#define HWRUN_EINPROGRESS (-EINPROGRESS)
#define HWRUN_EINVAL (-EINVAL)

/* HWRun 私有错误域：标准 errno 之上（Linux 上限约 133），负号使用 */
#define HW_EBASE 0x1000
#define HWRUN_ECONFLICT (-(HW_EBASE + 1)) /* 依赖/注册冲突 */
#define HWRUN_ENOTREADY (-(HW_EBASE + 2)) /* 子系统未就绪 */

/* ============================================================
 * 插件状态（与微内核任务状态对应）
 * ============================================================ */
typedef enum {
    HWPLUGIN_UNINSTALLED = 0,
    HWPLUGIN_INSTALLED, /* 已安装，未加载 */
    HWPLUGIN_LOADED,    /* 已加载，未启动 */
    HWPLUGIN_STARTED,   /* 运行中 */
    HWPLUGIN_STOPPED,   /* 已停止，保留状态 */
    HWPLUGIN_ERROR,
} hwplugin_state_t;

/* ============================================================
 * 插件类型
 * ============================================================ */
typedef enum {
    HWPLUGIN_TYPE_KERNEL = 0,
    HWPLUGIN_TYPE_LIBC,
    HWPLUGIN_TYPE_INIT,
    HWPLUGIN_TYPE_FS,
    HWPLUGIN_TYPE_NETWORK,
    HWPLUGIN_TYPE_SECURITY,
    HWPLUGIN_TYPE_STORAGE,
    HWPLUGIN_TYPE_LOGGING,
    HWPLUGIN_TYPE_SCHEDULER,
    HWPLUGIN_TYPE_GIT,
    HWPLUGIN_TYPE_LOADER,
    HWPLUGIN_TYPE_UI,
    HWPLUGIN_TYPE_TOOLS,
    HWPLUGIN_TYPE_MANAGEMENT,
    HWPLUGIN_TYPE_DRIVER,
    HWPLUGIN_TYPE_CONTAINER,
    HWPLUGIN_TYPE_CRYPTO,
    HWPLUGIN_TYPE_SERVICE,
    HWPLUGIN_TYPE_UNKNOWN,
} hwplugin_type_t;

/* ============================================================
 * 插件描述符（完整版，对应 plugin.yml）
 * ============================================================ */
typedef struct hw_plugin hw_plugin_t;
typedef struct hw_param hw_param_t;
typedef struct hw_runtime hw_runtime_t; /* 前向声明：runtime_bind 字段使用 */

/* 生命周期回调 —— 每个插件必须实现 */
typedef struct hw_plugin_ops {
    int (*init)(hw_plugin_t *self);
    int (*start)(hw_plugin_t *self);
    int (*stop)(hw_plugin_t *self);
    int (*destroy)(hw_plugin_t *self);
    /* 参数变更时回调：key 形如 "fs.ext4.enable"；返回 0 成功 */
    int (*configure)(hw_plugin_t *self, const char *key, const char *value);
    /* 取得对外提供的协议接口实现指针 */
    void *(*get_interface)(const char *protocol);
} hw_plugin_ops_t;

struct hw_plugin {
    char id[64]; /* "linux-kernel"           */
    char name[128];
    char version[32];
    hwplugin_type_t type;
    hwplugin_state_t state;
    char description[256];

    /* Git */
    char repo_url[256], branch[64], tag[64];

    /* 钩子脚本 */
    char pre_install[128];
    char post_install[128];
    char pre_uninstall[128];
    char post_uninstall[128];

    /* 协议 */
    char **provides;
    int provides_count;
    char **requires;
    int requires_count;
    char **conflicts;
    int conflicts_count;

    /* 文件 */
    char **files;
    int files_count;
    size_t size;

    /* 生命周期 */
    hw_plugin_ops_t ops;

    /* 内部 */
    void *handle; /* dlopen 句柄    */
    void *private_data;
    uint64_t load_time;
    int ref_count;
    uint64_t task_id;
    hw_param_t *params; /* 默认参数链表 */

    /* 运行时注入绑定点：由 entry() 填充本 .so 的 bind 地址，
     * loader/测试经此字段调用，避免同名符号在 RTLD_GLOBAL 下冲突 */
    void (*runtime_bind)(hw_runtime_t *rt);

    struct hw_plugin *next, *prev;
};

/* ============================================================
 * 协议路由（METAPROTO 注册表条目）
 * ============================================================ */
typedef struct hw_protocol_route {
    char protocol[64];
    char version[16];
    char plugin_id[64];
    void *implementation;    /* 实现接口指针 */
    uint16_t provider_state; /* 提供者插件状态 */
    struct hw_protocol_route *next;
} hw_protocol_route_t;

/* ============================================================
 * 协议依赖
 * ============================================================ */
typedef struct hw_protocol_dep {
    char protocol[64];
    char version[16];
    struct hw_protocol_dep *next;
} hw_protocol_dep_t;

/* ============================================================
 * 协议订阅者（协议变更通知）
 * ============================================================ */
typedef struct hw_protocol_sub {
    char plugin_id[64];
    char protocol[64]; /* 订阅的协议；空=全部 */
    void (*on_protocol_change)(const char *protocol, const char *version, const char *plugin_id,
                               int state);
    struct hw_protocol_sub *next;
} hw_protocol_sub_t;

/* ============================================================
 * 参数
 * ============================================================ */
enum {
    HWPARAM_TYPE_STRING = 0,
    HWPARAM_TYPE_INT,
    HWPARAM_TYPE_BOOL,
    HWPARAM_TYPE_FLOAT,
    HWPARAM_TYPE_LIST,
};

struct hw_param {
    char key[128]; /* "scheduler.policy"        */
    char value[256];
    int type;
    char description[256];
    struct hw_param *child; /* 子参数               */
    struct hw_param *sibling;
    void *user_data;
};

/* ============================================================
 * 子系统句柄（由微内核/总线提供）
 * ============================================================ */
typedef struct hw_kernel_ops hw_kernel_ops_t; /* 前向声明，由微内核提供 */
typedef struct hw_bus_ops hw_bus_ops_t;

/* ============================================================
 * 标准协议名称常量（依赖链顺序）
 * ============================================================ */
#define HWPROTO_METAPROTO "METAPROTO"
#define HWPROTO_BUS "BUS"
#define HWPROTO_PARAM "PARAM"
#define HWPROTO_LOG "LOG"
#define HWPROTO_GIT "GIT"
#define HWPROTO_HAP "HAP"
#define HWPROTO_PMP "PMP"
#define HWPROTO_FSP "FSP"
#define HWPROTO_NP "NP"
#define HWPROTO_SP "SP"
#define HWPROTO_CRYPTO "CRYPTO"
#define HWPROTO_LOADER "LOADER"
#define HWPROTO_COMPRESS "COMPRESS"
#define HWPROTO_TERMINAL "TERMINAL"
#define HWPROTO_SHELL "SHELL"
#define HWPROTO_CONSOLE "CONSOLE"

/* ============================================================
 * 工具函数（由核心库 hwrun-core 提供）
 * ============================================================ */
extern char **hw_str_split(const char *s, const char *sep, int *count);
extern void hw_str_list_free(char **list, int count);
extern char *hw_strdup(const char *s);
extern char *hw_strndup(const char *s, size_t n);
extern int hw_str_eq(const char *a, const char *b);
extern int hw_fmt_path(char *out, size_t cap, const char *dir, const char *name);
extern const char *hw_strerror(int rc); /* 负 errno / 私有域 -> 可读描述 */

/* 参数树 */
extern hw_param_t *hw_param_find(hw_param_t *root, const char *key);
extern hw_param_t *hw_param_get_child(hw_param_t *p, const char *key);
extern const char *hw_param_value(hw_param_t *root, const char *key, const char *def);
extern int hw_param_set(hw_param_t *root, const char *key, const char *value, int type,
                        const char *desc);
extern hw_param_t *hw_param_add_child(hw_param_t *parent, const char *key, const char *value,
                                      int type, const char *desc);

/* ============================================================
 * 运行时注入（BUS 在 init 前注入；插件经 hw_plugin_runtime_get() 获取）
 *
 * 插件 .so 自包含，只依赖本契约。BUS 通过 dlsym(句柄, "hw_plugin_runtime_bind")
 * 把构造好的 hw_runtime_t 投递给插件内部的静态副本，插件随后经
 * hw_plugin_runtime_get() 使用。未注入 / 回调为 NULL 时便捷宏全部静默降级。
 * ============================================================ */
typedef struct hw_runtime hw_runtime_t;

/* 运行时日志级别（与 bus/log.h 的 HWLOG_* 数值一致，供插件独立使用） */
enum {
    HWAPI_LOG_DEBUG = 0,
    HWAPI_LOG_INFO = 1,
    HWAPI_LOG_WARN = 2,
    HWAPI_LOG_ERROR = 3,
    HWAPI_LOG_FATAL = 4,
};

struct hw_runtime {
    /* ---- LOG ---- */
    void (*log)(int level, const char *plugin_id, const char *fmt, ...);

    /* ---- PARAM ---- */
    const char *(*param_get)(const char *key);
    int (*param_get_int)(const char *key, int def);
    bool (*param_get_bool)(const char *key, bool def);
    int (*param_set)(const char *key, const char *value, int type, const char *desc);
    int (*param_watch)(const char *plugin_id, const char *pattern,
                       int (*cb)(const char *, const char *, const char *, void *), void *userdata);

    /* ---- GIT（签名取自 git/include/git.h） ---- */
    int (*git_add)(const char *path);
    int (*git_commit)(const char *message, char *out_id, size_t cap);
    char *(*git_status)(void); /* 返回值需要 free 后释放 */
    void *git;                 /* 原始 hw_git_ops_t*，需更多能力时强转 */

    void *ctx; /* 保留，未用 */
};

/* 每插件 .so 须在自己的入口 .c 实现一次（静态存储） */
extern void hw_plugin_runtime_bind(hw_runtime_t *rt);
extern hw_runtime_t *hw_plugin_runtime_get(void);

/* ===== 便捷宏（插件 #include hwrun.h 后即可使用，NULL 安全） ===== */
#define HWAPI_R() hw_plugin_runtime_get()
#define HWAPI_LOG(level, plug, ...)                                                                \
    do {                                                                                           \
        hw_runtime_t *_r = HWAPI_R();                                                              \
        if (_r && _r->log) _r->log((level), (plug), __VA_ARGS__);                                  \
    } while (0)
#define HWAPI_LOGI(plug, ...) HWAPI_LOG(HWAPI_LOG_INFO, (plug), __VA_ARGS__)
#define HWAPI_LOGW(plug, ...) HWAPI_LOG(HWAPI_LOG_WARN, (plug), __VA_ARGS__)
#define HWAPI_LOGE(plug, ...) HWAPI_LOG(HWAPI_LOG_ERROR, (plug), __VA_ARGS__)

#define HWAPI_PARAM_GET(key)                                                                       \
    (HWAPI_R() && HWAPI_R()->param_get ? HWAPI_R()->param_get((key)) : NULL)
#define HWAPI_PARAM_GET_INT(key, def)                                                              \
    (HWAPI_R() && HWAPI_R()->param_get_int ? HWAPI_R()->param_get_int((key), (def)) : (def))
#define HWAPI_PARAM_GET_BOOL(key, def)                                                             \
    (HWAPI_R() && HWAPI_R()->param_get_bool ? HWAPI_R()->param_get_bool((key), (def)) : (def))
#define HWAPI_PARAM_SET(key, val, type, desc)                                                      \
    (HWAPI_R() && HWAPI_R()->param_set ? HWAPI_R()->param_set((key), (val), (type), (desc))        \
                                       : HWRUN_EINVAL)

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_H */