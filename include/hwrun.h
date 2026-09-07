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

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 版本
 * ============================================================ */
#define HWRUN_VERSION            "7.0.0"
#define HWRUN_PROTOCOL_VERSION   "1.0"

/* ============================================================
 * 错误码
 * ============================================================ */
enum {
    HWRUN_OK           = 0,
    HWRUN_EINVAL       = -1,
    HWRUN_ENOENT       = -2,
    HWRUN_ENOMEM       = -3,
    HWRUN_EEXIST       = -4,
    HWRUN_ENOTSUP      = -5,
    HWRUN_EILSEQ       = -6,   /* 协议不匹配 */
    HWRUN_ECONFLICT    = -7,   /* 依赖冲突 */
    HWRUN_EAGAIN       = -8,
    HWRUN_EINPROGRESS  = -9,
    HWRUN_EPERM        = -10,
    HWRUN_ENOTREADY    = -11,
};

/* ============================================================
 * 插件状态（与微内核任务状态对应）
 * ============================================================ */
typedef enum {
    HWPLUGIN_UNINSTALLED = 0,
    HWPLUGIN_INSTALLED,   /* 已安装，未加载 */
    HWPLUGIN_LOADED,      /* 已加载，未启动 */
    HWPLUGIN_STARTED,     /* 运行中 */
    HWPLUGIN_STOPPED,     /* 已停止，保留状态 */
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
    HWPLUGIN_TYPE_UNKNOWN,
} hwplugin_type_t;

/* ============================================================
 * 插件描述符（完整版，对应 plugin.yml）
 * ============================================================ */
typedef struct hw_plugin hw_plugin_t;
typedef struct hw_param  hw_param_t;

/* 生命周期回调 —— 每个插件必须实现 */
typedef struct hw_plugin_ops {
    int  (*init)(hw_plugin_t *self);
    int  (*start)(hw_plugin_t *self);
    int  (*stop)(hw_plugin_t *self);
    int  (*destroy)(hw_plugin_t *self);
    /* 参数变更时回调：key 形如 "fs.ext4.enable"；返回 0 成功 */
    int  (*configure)(hw_plugin_t *self, const char *key, const char *value);
    /* 取得对外提供的协议接口实现指针 */
    void*(*get_interface)(const char *protocol);
} hw_plugin_ops_t;

struct hw_plugin {
    char              id[64];        /* "linux-kernel"           */
    char              name[128];
    char              version[32];
    hwplugin_type_t   type;
    hwplugin_state_t  state;
    char              description[256];

    /* Git */
    char  repo_url[256], branch[64], tag[64];

    /* 钩子脚本 */
    char  pre_install[128];
    char  post_install[128];
    char  pre_uninstall[128];
    char  post_uninstall[128];

    /* 协议 */
    char **provides;   int provides_count;
    char **requires;   int requires_count;
    char **conflicts;  int conflicts_count;

    /* 文件 */
    char **files;      int files_count;
    size_t  size;

    /* 生命周期 */
    hw_plugin_ops_t ops;

    /* 内部 */
    void              *handle;        /* dlopen 句柄    */
    void              *private_data;
    uint64_t           load_time;
    int                ref_count;
    uint64_t           task_id;
    hw_param_t        *params;        /* 默认参数链表 */

    struct hw_plugin *next, *prev;
};

/* ============================================================
 * 协议路由（METAPROTO 注册表条目）
 * ============================================================ */
typedef struct hw_protocol_route {
    char    protocol[64];
    char    version[16];
    char    plugin_id[64];
    void   *implementation;           /* 实现接口指针 */
    uint16_t provider_state;          /* 提供者插件状态 */
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
    char protocol[64];                    /* 订阅的协议；空=全部 */
    void (*on_protocol_change)(const char *protocol, const char *version,
                               const char *plugin_id, int state);
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
    char     key[128];      /* "scheduler.policy"        */
    char     value[256];
    int      type;
    char     description[256];
    struct hw_param *child; /* 子参数               */
    struct hw_param *sibling;
    void    *user_data;
};

/* ============================================================
 * 子系统句柄（由微内核/总线提供）
 * ============================================================ */
typedef struct hw_kernel_ops hw_kernel_ops_t;   /* 前向声明，由微内核提供 */
typedef struct hw_bus_ops     hw_bus_ops_t;

/* ============================================================
 * 标准协议名称常量（依赖链顺序）
 * ============================================================ */
#define HWPROTO_METAPROTO   "METAPROTO"
#define HWPROTO_BUS         "BUS"
#define HWPROTO_PARAM       "PARAM"
#define HWPROTO_LOG         "LOG"
#define HWPROTO_GIT         "GIT"
#define HWPROTO_HAP         "HAP"
#define HWPROTO_PMP         "PMP"
#define HWPROTO_FSP         "FSP"
#define HWPROTO_NP          "NP"
#define HWPROTO_SP          "SP"
#define HWPROTO_CRYPTO      "CRYPTO"
#define HWPROTO_LOADER      "LOADER"

/* ============================================================
 * 工具函数（由核心库 hwrun-core 提供）
 * ============================================================ */
extern char **hw_str_split(const char *s, const char *sep, int *count);
extern void   hw_str_list_free(char **list, int count);
extern char  *hw_strdup(const char *s);
extern char  *hw_strndup(const char *s, size_t n);
extern int    hw_str_eq(const char *a, const char *b);
extern int    hw_fmt_path(char *out, size_t cap, const char *dir, const char *name);

/* 参数树 */
extern hw_param_t *hw_param_find(hw_param_t *root, const char *key);
extern hw_param_t *hw_param_get_child(hw_param_t *p, const char *key);
extern const char *hw_param_value(hw_param_t *root, const char *key,
                                  const char *def);
extern int         hw_param_set(hw_param_t *root, const char *key,
                                const char *value, int type,
                                const char *desc);
extern hw_param_t *hw_param_add_child(hw_param_t *parent, const char *key,
                                      const char *value, int type,
                                      const char *desc);

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_H */