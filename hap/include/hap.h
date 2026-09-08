/*
 * hap.h — HWRun OS 硬件抽象协议（HAP）公共接口
 *
 * 本头文件定义 HAP 插件对外暴露的数据结构与接口。
 * 上层插件（FSP/PMP/NP/MONITOR/...）通过 get_interface("HAP")
 * 获取 hw_hap_ops_t，从而统一访问 CPU / 内存 / 磁盘 / 网络 / 系统
 * 等硬件与系统信息，不直接操作硬件。
 *
 * 设计原则：协议驱动、统一抽象、按需加载、只提供"机制"不提供"策略"。
 * 在 MSYS2/Windows 等降级环境下，读取不到的数据一律填 0 / 空串，绝不崩溃。
 */

#ifndef HW_HAP_H
#define HW_HAP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * HAP 返回码（复用 HWRun 错误码语义，0 表示成功）
 * ============================================================ */
enum {
    HAP_OK = 0,
    HAP_USERES = 1, /* HAP 插件尚未就绪 */
};

/* ============================================================
 * CPU 信息
 * ============================================================ */
typedef struct hap_cpu_info {
    char vendor[64];           /* "GenuineIntel" / "AuthenticAMD" / "" */
    char model[128];           /* "Intel Core i7-12700K" 等 */
    uint32_t cores;            /* 逻辑 CPU 数量（可在线核心数） */
    uint32_t sockets;          /* 物理插槽数（降级时取 1） */
    uint64_t freq_current_mhz; /* 当前频率（近似） */
    uint64_t freq_min_mhz;     /* 最低频率 */
    uint64_t freq_max_mhz;     /* 最高频率 */
    /* 负载均值（来自 /proc/loadavg），单位：运行队列中的任务数 */
    double load1m;
    double load5m;
    double load15m;
    /* 串行化的 CPU 标识，供 log 使用 */
    char ident[256];
} hap_cpu_info_t;

/* ============================================================
 * 内存信息
 * ============================================================ */
typedef struct hap_memory_info {
    uint64_t total_kb;      /* 物理内存总量 */
    uint64_t free_kb;       /* 空闲内存 */
    uint64_t available_kb;  /* 可用内存（考虑缓存回收） */
    uint64_t used_kb;       /* 已用内存 = total - available */
    uint64_t swap_total_kb; /* 交换分区总量 */
    uint64_t swap_free_kb;  /* 交换分区空闲 */
    char memory_type[32];   /* 内存类型（降级环境下未知） */
} hap_memory_info_t;

/* ============================================================
 * 磁盘设备信息
 * ============================================================ */
typedef struct hap_disk_device {
    char name[64];         /* 设备名：sda / nvme0n1 / /dev/... */
    char model[128];       /* 型号（可为空） */
    uint64_t capacity_mb;  /* 总容量（MB） */
    uint64_t used_mb;      /* 已用（MB，有挂载点时有效） */
    uint64_t free_mb;      /* 可用（MB，有挂载点时有效） */
    double use_pct;        /* 使用率（0-100，无数据时 0） */
    char mount_point[256]; /* 挂载点（可为空） */
    char fs_type[32];      /* 文件系统类型（可为空） */
    int available;         /* 是否有容量/使用数据 */
} hap_disk_device_t;

typedef struct hap_disk_info {
    uint32_t device_count; /* 实际设备数 */
    hap_disk_device_t devices[32];
} hap_disk_info_t;

/* ============================================================
 * 网络接口信息
 * ============================================================ */
typedef struct hap_net_iface {
    char name[64];  /* 接口名：eth0 / wlan0 / lo */
    int is_up;      /* admin up */
    int is_running; /* carrier running */
    uint32_t mtu;
    char mac[32]; /* 硬件地址（可为空） */
    char ip[64];  /* 首个 IPv4 地址（可为空） */
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint32_t flags; /* 原始 IFF_* 标志位 */
} hap_net_iface_t;

typedef struct hap_net_info {
    uint32_t iface_count;
    hap_net_iface_t ifaces[64];
} hap_net_info_t;

/* ============================================================
 * 系统信息
 * ============================================================ */
typedef struct hap_system_info {
    char hostname[128];       /* 主机名 */
    char kernel_release[256]; /* 内核版本（uname -r） */
    char kernel_version[256]; /* 完整内核版本（uname -v） */
    char machine[64];         /* 架构：x86_64 / aarch64 */
    char os_name[256];        /* 操作系统名称（/etc/os-release） */
    uint64_t uptime_seconds;  /* 运行时长（秒） */
} hap_system_info_t;

/* ============================================================
 * HAP 对外协议接口（get_interface("HAP") 返回此结构）
 * ============================================================ */
typedef struct hw_hap_ops {
    int (*get_cpu_info)(hap_cpu_info_t *out);
    int (*get_memory_info)(hap_memory_info_t *out);
    int (*get_disk_info)(hap_disk_info_t *out);
    int (*get_net_info)(hap_net_info_t *out);
    int (*get_system_info)(hap_system_info_t *out);
} hw_hap_ops_t;

/* 供插件内部使用的探测函数声明（各 *_impl 由 src/ 实现） */
int hap_cpu_probe(hap_cpu_info_t *out);
int hap_memory_probe(hap_memory_info_t *out);
int hap_disk_probe(hap_disk_info_t *out);
int hap_net_probe(hap_net_info_t *out);
int hap_system_probe(hap_system_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* HW_HAP_H */