/*
 * kctl.h — BUS 内核边界客户端
 *
 * 通过 /dev/hwrun 访问 HWRun 内核边界模块 (hwrun_core.ko)，
 * 提供 ABI 版本查询、ping、协议注册/注销/解析。
 *
 * 设计：按需访问，非强依赖。当 /dev/hwrun 不存在或 ioctl 失败时，
 * 客户端函数统一返回 HWRUN_ENOTREADY / 具体 errno，不崩溃。
 * 用户态 METAPROTO 仍是主路径；本客户端是对内核桥接的可选调用。
 */

#ifndef HWRUN_KCTL_H
#define HWRUN_KCTL_H

#include "hwrun.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 与 kernel/include/uapi/hwrun.h 对齐 */
#define HWRUN_KCTL_DEVICE          "/dev/hwrun"
#define HWRUN_KCTL_ABI_VERSION     1
#define HWRUN_KCTL_PROTOCOL_NAME_MAX   64
#define HWRUN_KCTL_PROTOCOL_VERSION_MAX 16
#define HWRUN_KCTL_PROVIDER_NAME_MAX   64

typedef struct hwrun_kctl_desc {
    char protocol[HWRUN_KCTL_PROTOCOL_NAME_MAX];
    char version[HWRUN_KCTL_PROTOCOL_VERSION_MAX];
    char provider[HWRUN_KCTL_PROVIDER_NAME_MAX];
    uint64_t implementation;
} hwrun_kctl_desc_t;

/* 客户端句柄：持有已打开的设备 fd */
typedef struct hwrun_kctl {
    int fd;           /* -1 = 未连接 */
    char device[128];
    int  connected;
} hwrun_kctl_t;

/* ---- 生命周期 ---- */
extern int hwrun_kctl_open(hwrun_kctl_t *c);          /* 打开 /dev/hwrun（可多次） */
extern int hwrun_kctl_open_path(hwrun_kctl_t *c, const char *path);
extern void hwrun_kctl_close(hwrun_kctl_t *c);

/* ---- 内核协议操作 ---- */
extern int hwrun_kctl_get_abi(hwrun_kctl_t *c, uint32_t *abi);          /* HWRUN_IOC_GET_ABI */
extern int hwrun_kctl_ping(hwrun_kctl_t *c, uint32_t *value);           /* HWRUN_IOC_PING   */
extern int hwrun_kctl_protocol_register(hwrun_kctl_t *c, const char *protocol,
                                        const char *version, const char *provider);
extern int hwrun_kctl_protocol_unregister(hwrun_kctl_t *c, const char *protocol,
                                          const char *provider);
extern int hwrun_kctl_protocol_resolve(hwrun_kctl_t *c, const char *protocol,
                                       hwrun_kctl_desc_t *out);

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_KCTL_H */