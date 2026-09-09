/*
 * audit.h — HWRun OS AUDIT 协议插件公共接口
 *
 * AUDIT (Audit Protocol) 是系统安全审计协议：用户态事件审计。任何上层子系统
 * （PERMISSION / SP / PKGMGR / ...）把"谁、何时、做了什么动作、对象是什么、
 * 结果如何、补充详情"通过 ops.append() 显式记录，由本插件持久化为只追加的
 * 审计日志文件，并提供条件查询与导出。
 *
 * 依据根目录设计稿《HWRun OS AUDIT.txt》（状态：设计冻结）落地 P0 用户态
 * 事件审计。与设计稿的映射与边界：
 *   - 设计稿 §5.5 的"内核 LSM 审计点 / 事件自动采集"不在 P0：当前无内核钩子；
 *     需要审计的代码路径在用户态显式调用 ops.append() 记录（显式记录 API）。
 *   - 设计稿的事件类型枚举/严重级别/规则引擎(§5.4)/速率限制不在 P0：v1 用
 *     自由文本 action 表达"何动作"，subject/target/detail 承载其余语义；
 *     规则过滤、类型/级别维度与策略管理（GetPolicy/SetPolicy/AddRule/...）
 *     留待 v2。
 *   - 设计稿协议 rpc → v1 ops：
 *       Log    → append()（返回分配的事件 seq）
 *       Query  → query()（主体/动作/结果/时间区间过滤 + 排序 + 上限）
 *       Export → export_events()
 *       Tail   → query(order=DESC, limit=N)（v1 以查询表达）
 *       Rotate → append 达到 max_bytes 时自动滚动（容量控制）
 *       Status → status()
 *
 * 与 GIT 插件的 git/src/audit.c 无任何关系（那是 GIT 内部的操作审计日志查询，
 * 非协议插件）。本插件是独立的 AUDIT 协议插件，本头为其对外公共接口。
 *
 * 存储格式（每行一条事件，追加写入，UTF-8 文本，字段以 '|' 分隔）：
 *   seq|ts|subject|action|target|result|detail\n
 *   - seq    事件序号（本 store 内单调递增，跨滚动段连续，进程重启后按日志回填）
 *   - ts     Unix 秒时间戳（append 时 <=0 自动取当前时间）
 *   - result 0=成功；<0=负 errno 失败语义（如 -EACCES）
 *   - detail 可选；为空时行尾保留分隔符（...|result|\n）
 *   - 转义：字符串字段内的 '\\'、'|'、'\n'、'\r' 分别编码为 \\、\|、\n、\r，
 *     读取时原样还原，因此任意 detail 文本都可安全记录
 *
 * 日志目录 / 滚动（容量控制）：
 *   - 默认日志目录为审计状态目录（HW_AUDIT_DEFAULT_DIR），主文件 <dir>/audit.log；
 *   - 目录与容量可用参数风格覆盖（键见 HW_AUDIT_PARAM_*，插件 configure 回调
 *     或总线 PARAM 注入均可生效）；
 *   - 单文件超过 max_bytes 字节时自动滚动：audit.log → .1 → .2 ...，仅保留
 *     max_files 个段（最旧段丢弃、保留最新），与设计稿 max_size/max_files 语义一致。
 *
 * 错误约定：成功返回 HWRUN_OK(0)；失败返回负 errno（-EINVAL/-ENOMEM/-EIO/
 * -EACCES/-errno）。查询结果数组由实现分配，调用方用 ops.events_free() 释放。
 *
 * 线程安全：单进程单实例全局存储，append/query/export/configure 内部互斥；
 * 不支持多进程写同一目录。
 */

#ifndef HWRUN_AUDIT_H
#define HWRUN_AUDIT_H

#include <stdint.h>
#include <stddef.h>

#include "hwrun.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 字段容量与默认配置
 * ============================================================ */
#define HW_AUDIT_SUBJECT_MAX 64 /* 主体：用户/服务名最大长度（不含 NUL） */
#define HW_AUDIT_ACTION_MAX 64  /* 动作描述最大长度 */
#define HW_AUDIT_TARGET_MAX 256 /* 对象/资源描述最大长度 */
#define HW_AUDIT_DETAIL_MAX 512 /* 详情最大长度 */
#define HW_AUDIT_DIR_MAX 512    /* 日志目录路径最大长度 */
#define HW_AUDIT_SEG_MAX 16     /* 滚动段数硬上限（含主文件） */

#define HW_AUDIT_DEFAULT_DIR "/var/lib/hwrun/audit" /* 默认审计状态目录 */
#define HW_AUDIT_DEFAULT_MAX_BYTES (1024u * 1024u)  /* 单文件 1MB 上限 */
#define HW_AUDIT_DEFAULT_MAX_FILES 3                /* 保留段数（含主文件） */

/* 参数键（"参数风格"路径覆盖：总线 PARAM 树或插件 configure 回调同名键） */
#define HW_AUDIT_PARAM_PATH "audit.log.path"           /* string：日志目录 */
#define HW_AUDIT_PARAM_MAX_BYTES "audit.log.max_bytes" /* int/带单位串：单文件上限 */
#define HW_AUDIT_PARAM_MAX_FILES "audit.log.max_files" /* int：保留段数 */

/* ============================================================
 * 审计事件
 *
 * 对应设计稿 audit_event_t（user→subject、target→对象/资源、
 * reason→detail、result 保留 0/负 errno 语义；task_id/uid/session/source_ip
 * 等字段可按需并入 detail，P0 不单列）。
 * ============================================================ */
typedef struct hw_audit_event {
    uint64_t seq;   /* 事件序号（由实现分配，追加前填 0 即可） */
    int64_t ts;     /* 时间戳（Unix 秒）；<=0 时 append 自动取当前时间 */
    int32_t result; /* 结果：0=成功；<0=负 errno 失败语义 */
    char subject[HW_AUDIT_SUBJECT_MAX]; /* 主体（谁）：必填非空 */
    char action[HW_AUDIT_ACTION_MAX];   /* 动作（做了什么）：必填非空 */
    char target[HW_AUDIT_TARGET_MAX];   /* 对象/资源（可空） */
    char detail[HW_AUDIT_DETAIL_MAX];   /* 详情（可空，如失败原因/补充信息） */
} hw_audit_event_t;

/* ============================================================
 * 查询条件
 *
 * 空字符串/0 表示"不过滤"；多个条件为 AND 关系。
 * ============================================================ */
#define HW_AUDIT_ORDER_ASC 0  /* 按 seq 升序（旧→新，默认） */
#define HW_AUDIT_ORDER_DESC 1 /* 按 seq 降序（新→旧） */

typedef struct hw_audit_query {
    int64_t start_ts;                   /* 起：>= start_ts（0 不限） */
    int64_t end_ts;                     /* 止：<= end_ts（0 不限） */
    char subject[HW_AUDIT_SUBJECT_MAX]; /* 主体精确匹配（空=不限） */
    char action[HW_AUDIT_ACTION_MAX];   /* 动作精确匹配（空=不限） */
    int result;                         /* 结果过滤值（仅当 result_set=1 时生效） */
    int result_set;                     /* 1=按 result 精确过滤（含 0）；0=不过滤 */
    int order;                          /* HW_AUDIT_ORDER_ASC / HW_AUDIT_ORDER_DESC */
    int limit;                          /* 最多返回条数（0=不限） */
} hw_audit_query_t;

/* ============================================================
 * 运行状态（status() 输出）
 * ============================================================ */
typedef struct hw_audit_status {
    uint64_t total_events; /* 本 store 已分配的事件序号数（含已滚动丢弃的旧事件） */
    uint64_t total_bytes;       /* 当前保留的全部段字节合计 */
    uint64_t seq_next;          /* 下一可用事件序号 */
    int segments;               /* 当前保留的日志段数（含主文件） */
    char dir[HW_AUDIT_DIR_MAX]; /* 生效的日志目录 */
} hw_audit_status_t;

/* ============================================================
 * AUDIT 协议接口（get_interface("AUDIT") 返回此指针，进程内单实例）
 * ============================================================ */
typedef struct hw_audit_ops {
    int32_t (*version)(void);

    /* 追加一条审计事件。成功返回 0，*out_seq 带回分配的事件序号（>=1）；
     * subject/action 为空或超长、目录不可写等返回负 errno。 */
    int (*append)(const hw_audit_event_t *ev, uint64_t *out_seq);

    /* 条件查询。按过滤条件返回事件数组（升/降序、上限），调用方
     * events_free() 释放；无匹配返回 0 且 *out=NULL、*out_count=0。 */
    int (*query)(const hw_audit_query_t *q, hw_audit_event_t **out_events, int *out_count);

    /* 释放 query() 返回的事件数组 */
    void (*events_free)(hw_audit_event_t *events, int count);

    /* 把满足查询条件的事件按审计日志行格式导出到 path（覆盖写）。
     * 成功返回 0，*out_count 带回导出条数。 */
    int (*export_events)(const hw_audit_query_t *q, const char *path, uint64_t *out_count);

    /* 运行状态（目录/序号/保留段数与字节合计） */
    int (*status)(hw_audit_status_t *st);
} hw_audit_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_AUDIT_H */
