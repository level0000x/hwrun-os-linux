/*
 * sp_internal.h — SP 插件内部共享上下文与工具
 *
 * 仅 SP 插件（src 目录下各 .c 文件）内部使用；不对外暴露。
 * 提供密钥存储、用户表、锁、临时文件、openssl 调用等内部基础设施。
 */

#ifndef HW_SP_INTERNAL_H
#define HW_SP_INTERNAL_H

#include <pthread.h>
#include <stdint.h>

#include "sp.h" /* hw_sp_ops_t 及错误码 */

/* ============================================================
 * 密钥存储条目（内存中持有 PEM）
 * ============================================================ */
typedef struct sp_key_entry {
    uint32_t key_id; /* 自增密钥 ID */
    int algo;        /* sp_asym_t    */
    char desc[128];
    char *priv_pem; /* 私钥 PEM（内存持有） */
    char *pub_pem;  /* 公钥 PEM             */
    int revoked;
    uint64_t created_ms; /* 毫秒时间戳 */
    struct sp_key_entry *next;
} sp_key_entry_t;

/* ============================================================
 * 认证用户条目（密码以 加盐SHA-256 十六进制 存储，绝不明文）
 * ============================================================ */
typedef struct sp_user {
    char user[64];
    char salt_hex[80];  /* 随机盐（hex） */
    char hash_hex[160]; /* sha256(salt||password) hex */
    char roles[128];    /* 逗号分隔的角色 */
    int enabled;        /* 1=启用 0=禁用 */
    struct sp_user *next;
} sp_user_t;

/* ============================================================
 * SP 上下文（插件私有数据）
 * ============================================================ */
typedef struct sp_ctx {
    int initialized;
    char state_dir[512]; /* 用户/状态持久化目录 */

    /* 密钥存储 */
    sp_key_entry_t *keys;
    uint32_t next_key_id;

    /* 用户表 */
    sp_user_t *users;

    pthread_mutex_t lock;
} sp_ctx_t;

extern sp_ctx_t g_sp_ctx;

/* ============================================================
 * 工具接口（sp_util.c）
 * ============================================================ */
#define SP_TMPDIR_ENV "SP_TMPDIR"
#define SP_STATEDIR_ENV "SP_STATE_DIR"

/* 创建唯一临时文件（mkstemp），返回路径（需释放），fd 返回 -1 除非 out_fd 非空 */
char *sp_tmp_path(char *buf, size_t cap);
int sp_write_all(const char *path, const void *data, size_t len); /* 截断写入 */

/* 运行命令并捕获退出码；stdout/stderr 可重定向到文件（可 NULL） */
int sp_sh(const char *cmd, const char *stdout_file, const char *stderr_file);

/* 将 shell 参数单引号包装（防注入） */
const char *sp_q(const char *s, char *buf, size_t cap);

/* 十六进制编解码 */
void sp_to_hex(const uint8_t *in, size_t in_len, char *out); /* out=2n+1 */
int sp_from_hex(const char *hex, uint8_t *out, size_t out_cap, size_t *out_len);

/* base64 编码（12 位新行关闭） */
int sp_b64_encode(const uint8_t *in, size_t in_len, char *out, size_t cap);
int sp_b64_decode(const char *b64, uint8_t *out, size_t cap, size_t *out_len);

/* 常量时间相等比较 */
int sp_ct_eq(const uint8_t *a, const uint8_t *b, size_t n);

/* 读取文本文件到缓冲区（含 '\0'）；文件不存在返回 -1 */
int sp_read_text(const char *path, char *buf, size_t cap);

/* 当前毫秒时间戳 */
uint64_t sp_now_ms(void);

/* ============================================================
 * 各功能模块回调装配（由主入口 sp.c 统一调用）
 * ============================================================ */
void sp_hash_bind(hw_sp_ops_t *ops);
void sp_crypto_bind(hw_sp_ops_t *ops);
void sp_key_bind(hw_sp_ops_t *ops);
void sp_auth_bind(hw_sp_ops_t *ops);

/* 认证模块生命周期（初始化加载用户表 / 释放） */
int sp_auth_init(void);
int sp_auth_cleanup(void);
/* 释放密钥存储 */
void sp_keys_cleanup(void);

#endif /* HW_SP_INTERNAL_H */