/*
 * permission.h — HWRun OS PERMISSION 协议插件公共接口
 *
 * PERMISSION (Permission Management Protocol) 是权限管理协议，按设计稿
 * 《HWRun OS PERMIS.txt》（状态：设计冻结）落地其"用户态 RBAC 部分"：
 * 以 主体(subject：用户名或进程 uid 标签) → 角色(role) → 权限(action+resource)
 * 的 RBAC 模型做细粒度授权管理，默认拒绝。
 *
 * 设计稿中的用户账号/口令认证属于 SP（安全协议）范畴（sp.authenticate /
 * sp.add_user），本插件不做；内核态 LSM 钩子 / MAC 强制访问控制 /
 * Linux Capabilities 封装需要内核边界（薄 .ko），不在本用户态 .so 内实现。
 * 本插件只做策略层 RBAC：角色定义、主体-角色绑定、按 (动作,资源) 判权、
 * 策略状态文件持久化（重启可恢复）。真实裁决全在本插件内存中完成，
 * 绝无占位/假数据。
 *
 * 权限语义：
 *   - 每个角色持有若干权限条目，条目 = 动作模式(action pattern) + 资源模式
 *     (resource pattern)。动作模式沿用设计稿权限写法（如 "fs.read"、
 *     "task.submit"、"cluster.*"、"*.*"）；资源模式为空表示"任意资源"，
 *     否则按通配匹配（'*' 匹配任意串，'?' 匹配单字符；资源模式如
 *     目录子树 "/home/" 加 "*"、节点 "node-*"）。
 *   - check(subject, action, resource)：主体的任一角色存在条目同时匹配
 *     动作与资源即允许（返回 0）；否则默认拒绝（返回 -EACCES）。
 *
 * 错误约定：成功返回 HWRUN_OK(0)；失败返回负 errno（-EINVAL/-ENOENT/
 * -EEXIST/-ENOMEM/-ENOSPC/-EACCES/-EIO）。deny 一律为 -EACCES。
 *
 * 持久化：状态文件为简单可读文本（每行一个指令，'#' 注释），默认路径
 * $PERMISSION_STATE，否则 $HWRUN_STATE/permission.state，否则
 * /var/lib/hwrun/permission.state。每个写操作成功后自动写回（write-through），
 * 插件 start 时自动载入——重启可恢复。ops.save/load/set_state_file 供显式管理。
 *
 * 依赖链：METAPROTO → BUS → PARAM → LOG → PERMISSION
 * requires: LOG / PARAM / METAPROTO
 */

#ifndef HWRUN_PERMISSION_H
#define HWRUN_PERMISSION_H

#include "hwrun.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 字段容量上限（与设计稿 permission.h 对齐并放宽主体/资源）
 * ============================================================ */
#define HW_PERMISSION_NAME_MAX 64     /* 角色名 */
#define HW_PERMISSION_DESC_MAX 256    /* 角色描述 */
#define HW_PERMISSION_SUBJECT_MAX 128 /* 主体：用户名或进程 uid 标签 */
#define HW_PERMISSION_ACTION_MAX 64   /* 动作模式，如 "fs.read"、"cluster.*" */
#define HW_PERMISSION_RESOURCE_MAX 256 /* 资源模式（可含 '*' 通配与空格）；空=任意 */
#define HW_PERMISSION_ROLE_MAX_PERMS 64 /* 单角色权限条目上限 */

/* ============================================================
 * 权限条目：动作模式 + 资源模式
 * ============================================================ */
typedef struct hw_permission_perm {
    char action[HW_PERMISSION_ACTION_MAX];
    char resource[HW_PERMISSION_RESOURCE_MAX]; /* 空串 = 任意资源 */
} hw_permission_perm_t;

/* ============================================================
 * 角色快照（role_get / role_list 输出，调用方提供缓冲）
 * ============================================================ */
typedef struct hw_permission_role {
    char name[HW_PERMISSION_NAME_MAX];
    char description[HW_PERMISSION_DESC_MAX];
    int perm_count;
    hw_permission_perm_t perms[HW_PERMISSION_ROLE_MAX_PERMS];
} hw_permission_role_t;

/* ============================================================
 * 主体-角色绑定（binding_list 输出，扁平二元组）
 * ============================================================ */
typedef struct hw_permission_binding {
    char subject[HW_PERMISSION_SUBJECT_MAX];
    char role[HW_PERMISSION_NAME_MAX];
} hw_permission_binding_t;

/* ============================================================
 * PERMISSION 协议接口 (get_interface("PERMISSION") 返回此指针)
 * ============================================================ */
typedef struct hw_permission_ops {
    int32_t (*version)(void);

    /* ---- 角色定义管理 ---- */
    /* 新增角色；同名返回 -EEXIST；名字/描述超长或含空白/'\n'/'#' 返回 -EINVAL */
    int (*role_add)(const char *name, const char *description);
    /* 删除角色；级联解除所有主体的该角色绑定；不存在返回 -ENOENT */
    int (*role_remove)(const char *name);
    /* 向角色加权限条目；resource 为 NULL/"" = 任意资源；重复返回 -EEXIST */
    int (*role_add_perm)(const char *role, const char *action, const char *resource);
    /* 移除权限条目（须逐字段精确匹配）；不存在返回 -ENOENT */
    int (*role_remove_perm)(const char *role, const char *action, const char *resource);
    /* 读取角色快照到 out（调用方清零后传入）；不存在返回 -ENOENT */
    int (*role_get)(const char *name, hw_permission_role_t *out);
    /* 列出角色：复制 min(cap, 总数) 项到 out，*count 填总数 */
    int (*role_list)(hw_permission_role_t *out, int cap, int *count);

    /* ---- 主体-角色绑定 ---- */
    /* 给主体授予角色（主体为用户名或进程 uid 标签，无需预先登记）；
     * 角色不存在 -ENOENT；已绑定 -EEXIST */
    int (*grant)(const char *subject, const char *role);
    /* 撤销主体的角色；未绑定 -ENOENT */
    int (*revoke)(const char *subject, const char *role);
    /* 列出全部 主体-角色 绑定关系（扁平二元组） */
    int (*binding_list)(hw_permission_binding_t *out, int cap, int *count);

    /* ---- 权限检查（默认拒绝） ----
     * 返回 0 = 允许；-EACCES = 拒绝（主体无角色 / 无匹配权限 / 主体未知）；
     * 参数非法返回 -EINVAL。resource 可传 NULL（视为空串，仅匹配
     * 资源模式为空或 "*" 的条目）。 */
    int (*check)(const char *subject, const char *action, const char *resource);

    /* ---- 策略持久化 ---- */
    /* 设置状态文件路径；path 为 NULL 恢复默认（env 解析）。仅改路径不载入 */
    int (*set_state_file)(const char *path);
    /* 把当前内存策略快照写入 path（NULL=当前路径）；写失败返回负 errno */
    int (*save)(const char *path);
    /* 从 path（NULL=当前路径）载入并整体替换内存策略；文件不存在 -ENOENT */
    int (*load)(const char *path);
} hw_permission_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_PERMISSION_H */
