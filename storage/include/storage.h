/*
 * storage.h — HWRun OS STORAGE 协议插件公共接口（用户态卷管理）
 *
 * STORAGE (Storage Backend Protocol) 是存储后端统一抽象。依据根目录设计稿
 * 《HWRun OS STORAG.txt》（状态：设计冻结）落地其"用户态卷管理"范畴：
 *
 *   - 卷定义（名称 / 后端目录路径 / 容量上限可选 / 只读标志 / 创建时间）；
 *   - 卷注册表：create_volume 时确保后端目录存在，卷元数据经
 *     $HWRUN_STATE/storage.state 可读文本持久化，init 时整表重载（重启恢复）；
 *   - 卷生命周期：delete_volume（递归删除数据目录前做存在校验）、
 *     unregister_volume（注销：仅摘元数据，保留数据目录）；
 *   - 查询：list_volumes / get_volume；
 *   - 统计：volume_df —— statvfs 取卷目录所在文件系统容量/已用/可用，
 *     递归 du 取卷真实占用与文件数（全部真实值，绝无占位/假数据）；
 *   - 快照：目录级副本（真实逐字节复制），snapshot_create / snapshot_restore /
 *     snapshot_delete / list_snapshots，快照目录归插件自管（state_dir/snapshots）。
 *
 * 宿主 Linux 边界（本插件不实现，沿用宿主 Linux 现状，见设计稿"依赖/架构"）：
 *   - 块设备发现、Device Mapper、LVM、RAID、精简池（thin provisioning）与
 *     配额强制 → Linux 存储栈承担；本插件的"卷"即一个真实目录（后端目录路径）；
 *   - 格式化 / mkfs / mount / umount / resize 属块设备/文件系统层能力，
 *     由宿主 mount、FSP 等承担，不在本协议 ops 内。
 *
 * 依赖链：requires: LOG / PARAM / METAPROTO（与 COMPRESS 对齐；未使用 FSP，
 * 目录与统计操作直接走 POSIX 系统调用，插件 .so 自包含、零第三方依赖）。
 * 协议串：get_interface("STORAGE")；plugin.yml provides STORAGE/1.0。
 *
 * 错误约定：成功返回 HWRUN_OK(0)；失败一律返回负 errno
 * （-EINVAL/-ENOENT/-EEXIST/-ENOTDIR/-EPERM/-ENAMETOOLONG/...），
 * 可用 strerror(-rc) 取描述。所有 out 结构由调用方提供存储。
 */

#ifndef HWRUN_STORAGE_H
#define HWRUN_STORAGE_H

#include "hwrun.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 对外提供的 ops 表实例（定义于 storage_impl.c，入口 core 经此符号暴露） */
extern struct hw_storage_ops hw_storage_ops;

/* ============================================================
 * 卷定义（create_volume 输入）
 * ============================================================ */
typedef struct hw_storage_volume_def {
    const char *name; /* 卷名称：唯一寻址键，不得含 '/' */
    const char *path; /* 后端目录路径（绝对路径；create 时确保存在） */
    uint64_t size_limit; /* 容量上限(字节)，0=不限制（仅元数据记录，配额强制属宿主边界） */
    int readonly;            /* 只读标志：1=禁删数据/禁恢复写入 */
    const char *description; /* 描述（可为 NULL） */
} hw_storage_volume_def_t;

/* ============================================================
 * 卷信息（注册表条目 + 运行时派生字段）
 * ============================================================ */
typedef struct hw_storage_volume_info {
    char id[40];    /* 卷 ID（hex，urandom 生成） */
    char name[128]; /* 卷名称 */
    char path[512]; /* 后端目录路径 */
    char description[256];
    uint64_t size_limit; /* 容量上限(字节)，0=不限 */
    uint64_t created_at; /* 创建时间（epoch 秒） */
    int readonly;        /* 只读标志 */
    int exists;          /* 派生：后端目录当前是否真实存在且为目录 */
} hw_storage_volume_info_t;

/* ============================================================
 * 快照信息
 * ============================================================ */
typedef struct hw_storage_snapshot_info {
    char id[40];      /* 快照 ID（hex） */
    char name[128];   /* 快照名称（卷内唯一） */
    char volume[128]; /* 源卷名称 */
    char path[512];   /* 快照目录路径（state_dir/snapshots/<volume>/<name>） */
    uint64_t created_at;
    int active; /* 1=可用（创建成功后恒为 1；0 预留） */
} hw_storage_snapshot_info_t;

/* ============================================================
 * df 风格统计（statvfs 真实取值 + 递归 du）
 * ============================================================ */
typedef struct hw_storage_df {
    uint64_t size_limit; /* 卷配置容量上限(字节)，0=不限 */
    uint64_t used_bytes; /* 卷目录真实占用（目录树内常规文件字节和，du） */
    uint64_t file_count; /* 卷目录树内常规文件数 */
    /* 后端文件系统（卷目录所在挂载点）真实值，单位统一为字节 */
    uint64_t fs_block_size; /* statvfs.f_bsize（文件系统建议块大小） */
    uint64_t fs_total;      /* 总容量 = f_blocks * frsize */
    uint64_t fs_used;       /* 已用 = (f_blocks - f_bfree) * frsize */
    uint64_t fs_avail;      /* 可用（非特权视域）= f_bavail * frsize */
    uint64_t fs_files;      /* inode 总数 f_files */
    uint64_t fs_files_free; /* inode 可用 f_ffree */
} hw_storage_df_t;

/* ============================================================
 * STORAGE 协议接口（get_interface("STORAGE") 返回此指针）
 *
 * 所有 name_or_id 参数：先按卷 ID 精确匹配，未命中再按卷名称匹配。
 * 错误约定见文件头注释；out/count 数组由调用方提供存储。
 * ============================================================ */
typedef struct hw_storage_ops {
    int32_t (*version)(void);

    /* --- 卷注册表 / 生命周期 --- */
    /* 创建卷：校验名称/路径 → 确保后端目录存在(mkdir -p) → 登记 → 持久化 */
    int (*create_volume)(const hw_storage_volume_def_t *def, hw_storage_volume_info_t *out);
    /* 删除卷：存在校验后递归删除数据目录，并摘除注册表记录、持久化 */
    int (*delete_volume)(const char *name_or_id);
    /* 注销卷：仅摘除注册表记录并持久化，数据目录原样保留 */
    int (*unregister_volume)(const char *name_or_id);
    /* 列出全部卷到 out[cap]，*count 记总卷数（>cap 时仅写满 cap） */
    int (*list_volumes)(hw_storage_volume_info_t *out, int cap, int *count);
    /* 查询单卷（ID 或名称），含 exists 派生字段 */
    int (*get_volume)(const char *name_or_id, hw_storage_volume_info_t *out);

    /* --- 统计 --- */
    /* df：卷目录 du 占用 + 所在文件系统 statvfs 容量/已用/可用（真实值） */
    int (*volume_df)(const char *name_or_id, hw_storage_df_t *out);

    /* --- 快照（目录级真实副本） --- */
    /* 对卷目录做真实逐字节副本到自管快照区并登记、持久化 */
    int (*snapshot_create)(const char *name_or_id, const char *snapshot_name,
                           hw_storage_snapshot_info_t *out);
    /* 恢复：把快照内容整体写回源卷目录（target_volume 为 NULL 时回源卷）；
     * 目标卷不得为只读。恢复前清空目标卷目录内容。 */
    int (*snapshot_restore)(const char *snapshot_name, const char *target_volume);
    /* 删除快照：递归删除快照目录并摘除登记、持久化 */
    int (*snapshot_delete)(const char *snapshot_name);
    /* 列快照；volume（源卷名称）非 NULL 时只列该卷，NULL=全部 */
    int (*list_snapshots)(const char *volume, hw_storage_snapshot_info_t *out, int cap, int *count);
} hw_storage_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_STORAGE_H */
