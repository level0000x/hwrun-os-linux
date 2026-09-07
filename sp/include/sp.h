/*
 * sp.h — HWRun OS 安全协议（SP）公共接口
 *
 * 本头文件定义 SP 插件对外暴露的数据结构与协议接口。
 * 上层插件（PERMISSION / CLUSTER / FS_TRANSFER / CONTAINER /
 * NODE_DISCOVERY / CRYPTO / AUDIT ...）通过 get_interface("SP")
 * 获取 hw_sp_ops_t，从而统一进行认证、授权、加密、解密、哈希、
 * 签名、验签、密钥管理、完整性校验等安全操作。
 *
 * 设计原则：
 *   - 协议驱动：所有安全操作经 SP 统一入口，不直接操作底层加密器；
 *   - 真实功能：加解密/哈希/签名/密钥均基于本机可用安全原语（优先
 *     libcrypto，降级时封装 `openssl` 命令行），绝非占位/虚实现；
 *   - 安全默认：对称加密采用 AES-256-CBC + HMAC-SHA256（encrypt-then-MAC
 *     认证加密），密码加盐哈希存储，授权默认拒绝；
 *   - 失败语义：操作失败返回负 errno/HWRUN 错误码，绝不崩溃。
 *
 * CRYPTO（加密协议）是 SP 的加密子模块，逻辑上归属 SP，单独提供 CRYPTO 协议。
 */

#ifndef HW_SP_H
#define HW_SP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 复用 HWRun 核心错误码；SP 内部据此归类返回 */
enum {
    SP_OK        = 0,   /* 成功            */
    SP_EBADCRED   = -1, /* 凭据错误        */
    SP_ENOTFOUND  = -2, /* 未找到密钥/用户 */
    SP_ENOMEM     = -3, /* 内存不足        */
    SP_EEXIST     = -4, /* 实体已存在      */
    SP_EINVAL     = -5, /* 参数非法        */
    SP_ENOTSUP    = -6, /* 不支持          */
    SP_EACCES     = -7, /* 授权拒绝        */
    SP_EAUTH      = -8, /* 认证失败/验签失败 */
    SP_EIO        = -9, /* 底层加密器错误  */
};

/* ============================================================
 * 哈希算法类型
 * ============================================================ */
typedef enum {
    SP_HASH_SHA1 = 0,
    SP_HASH_SHA256,
    SP_HASH_SHA512,
    SP_HASH_SHA3_256,
    SP_HASH_SHA3_512,
} sp_hash_t;

/* ============================================================
 * 非对称密钥算法
 * ============================================================ */
typedef enum {
    SP_ASYM_RSA_2048 = 0,
    SP_ASYM_RSA_4096,
    SP_ASYM_ECDSA_P256,
} sp_asym_t;

/* ============================================================
 * 便捷常量
 * ============================================================ */
#define SP_MAX_HASH_LEN        64   /* SHA3-512 / SHA-512 输出长度 */
#define SP_MAX_KMODULUS        512  /* RSA-4096 PEM 典型长度上限 */
#define SP_SESSION_ID_LEN      80
#define SP_INTERNAL_KEY_BITS   256  /* 对称密钥默认位数(AES-256) */

/* ============================================================
 * 安全协议接口（get_interface("SP") 返回此结构）
 *
 * 说明：
 *  - 哈希由调用方提供输出缓冲区，函数回填长度；
 *  - 加密采用 AES-256-CBC+HMAC-SHA256（encrypt-then-MAC）；
 *    key 为调用方提供的密钥字节；iv 输入为期望的 IV（可传生成），
 *    tag 为 HMAC 认证标签；解密时校验标签失败返回 SP_EAUTH；
 *  - 密钥管理以自增 key_id 标识内存中的 PEM 密钥对；
 *  - 认证使用加盐 SHA-256，密码绝不明文存储。
 * ============================================================ */
typedef struct hw_sp_ops {
    /* ---------- 哈希与完整性 ---------- */
    int (*hash)(int algo, const uint8_t *data, uint32_t len,
                uint8_t *out, uint32_t *out_len);
    int (*hash_hex)(int algo, const uint8_t *data, uint32_t len,
                    char *out_hex, uint32_t hex_cap);
    int (*hash_file)(int algo, const char *path,
                     uint8_t *out, uint32_t *out_len);
    int (*hash_file_hex)(int algo, const char *path,
                         char *out_hex, uint32_t hex_cap);
    /* verify_hash：预期相等返回 0；不等返回 SP_EAUTH；出错返回负错误码 */
    int (*verify_hash)(int algo, const uint8_t *data, uint32_t len,
                       const uint8_t *expect, uint32_t expect_len);
    int (*verify_file_hash)(int algo, const char *path,
                            const char *expected_hex);

    /* ---------- 对称加解密（认证加密） ---------- */
    int (*encrypt)(const uint8_t *key, uint32_t key_len,
                   const uint8_t *iv, uint32_t iv_len,
                   const uint8_t *aad, uint32_t aad_len,
                   const uint8_t *pt, uint32_t pt_len,
                   uint8_t *ct, uint32_t *ct_len,
                   uint8_t *tag, uint32_t *tag_len);
    int (*decrypt)(const uint8_t *key, uint32_t key_len,
                   const uint8_t *iv, uint32_t iv_len,
                   const uint8_t *aad, uint32_t aad_len,
                   const uint8_t *ct, uint32_t ct_len,
                   const uint8_t *tag, uint32_t tag_len,
                   uint8_t *pt, uint32_t *pt_len);

    /* ---------- 密钥管理 ---------- */
    int (*gen_key_pair)(int asym_algo, const char *desc, uint32_t *key_id);
    int (*get_public_key)(uint32_t key_id, char *pem_out, uint32_t pem_cap);
    int (*export_private_key)(uint32_t key_id, char *pem_out, uint32_t pem_cap);
    int (*import_key_pair)(int asym_algo, const char *desc,
                           const char *priv_pem, uint32_t *key_id);
    int (*revoke_key)(uint32_t key_id);
    int (*destroy_key)(uint32_t key_id);
    int (*list_keys)(char *buf, uint32_t buf_cap);

    /* ---------- 签名与验签 ---------- */
    int (*sign)(uint32_t key_id, const uint8_t *data, uint32_t len,
                char *sig_b64_out, uint32_t b64_cap);
    int (*verify)(uint32_t key_id, const uint8_t *data, uint32_t len,
                  const char *sig_b64, int *valid);

    /* ---------- 认证与授权（RBAC） ---------- */
    int (*authenticate)(const char *user, const char *password,
                        char *session_id, uint32_t id_cap);
    int (*authorize)(const char *user, const char *action, const char *target);
    int (*add_user)(const char *user, const char *password,
                    const char *roles, int enabled);
    int (*del_user)(const char *user);
    int (*check_acl)(const char *user, const char *action, const char *target,
                     int *allowed);

    /* ---------- 随机数 ---------- */
    int (*random)(uint8_t *buf, uint32_t len);

    /* ---------- 自检 ---------- */
    int (*selftest)(void);
} hw_sp_ops_t;

/* 供插件内部使用的函数声明（各 *_impl 由 src/ 实现） */
int sp_hash_compute(int algo, const uint8_t *data, uint32_t len,
                    uint8_t *out, uint32_t *out_len);
int sp_hash_file(int algo, const char *path, uint8_t *out, uint32_t *out_len);

int sp_encrypt(const uint8_t *key, uint32_t key_len,
               const uint8_t *iv, uint32_t iv_len,
               const uint8_t *aad, uint32_t aad_len,
               const uint8_t *pt, uint32_t pt_len,
               uint8_t *ct, uint32_t *ct_len,
               uint8_t *tag, uint32_t *tag_len);
int sp_decrypt(const uint8_t *key, uint32_t key_len,
               const uint8_t *iv, uint32_t iv_len,
               const uint8_t *aad, uint32_t aad_len,
               const uint8_t *ct, uint32_t ct_len,
               const uint8_t *tag, uint32_t tag_len,
               uint8_t *pt, uint32_t *pt_len);

int sp_gen_key_pair(int asym_algo, const char *desc, uint32_t *key_id);
int sp_get_public_key(uint32_t key_id, char *pem_out, uint32_t pem_cap);
int sp_export_private_key(uint32_t key_id, char *pem_out, uint32_t pem_cap);
int sp_import_key_pair(int asym_algo, const char *desc,
                       const char *priv_pem, uint32_t *key_id);
int sp_revoke_key(uint32_t key_id);
int sp_destroy_key(uint32_t key_id);
int sp_list_keys(char *buf, uint32_t buf_cap);
int sp_sign(uint32_t key_id, const uint8_t *data, uint32_t len,
            char *sig_b64_out, uint32_t b64_cap);
int sp_verify(uint32_t key_id, const uint8_t *data, uint32_t len,
              const char *sig_b64, int *valid);

int sp_authenticate(const char *user, const char *password,
                    char *session_id, uint32_t id_cap);
int sp_authorize(const char *user, const char *action, const char *target);
int sp_add_user(const char *user, const char *password,
                const char *roles, int enabled);
int sp_del_user(const char *user);
int sp_check_acl(const char *user, const char *action, const char *target,
                 int *allowed);

int sp_random(uint8_t *buf, uint32_t len);
int sp_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* HW_SP_H */