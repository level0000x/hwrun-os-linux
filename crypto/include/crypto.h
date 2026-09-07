/*
 * crypto.h — HWRun OS CRYPTO 协议插件公共接口
 *
 * CRYPTO (Cryptographic Protocol) 是 SP（安全协议）的加密子模块，
 * 通过 CRYPTO 协议对外提供对称/非对称加密、哈希、HMAC、数字签名、
 * 密钥管理、安全随机数等全部密码学原语。
 *
 * 依赖链第 7 环：METAPROTO → BUS → PARAM → LOG → SP → CRYPTO
 * requires: SP / PARAM / LOG / METAPROTO
 *
 * 实现方案：优先调用 libcrypto (OpenSSL EVP)。本仓库构建环境下
 * 使用 MSYS2 mingw64 工具链及其自带的 libcrypto。绝无假数据/占位。
 *
 * 错误约定：所有函数失败时返回负 errno（如 -HWRUN_EINVAL），成功返回 HWRUN_OK(0)。
 */

#ifndef HWRUN_CRYPTO_H
#define HWRUN_CRYPTO_H

#include "hwrun.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 对称密码算法标识 (映射到 OpenSSL EVP / EVP_CIPHER)
 * ============================================================ */
typedef enum {
    HWRUN_CRYPTO_CIPHER_AES_128_CBC = 0,   /* AES-128-CBC (PKCS7 填充) */
    HWRUN_CRYPTO_CIPHER_AES_256_CBC,       /* AES-256-CBC (PKCS7 填充) */
    HWRUN_CRYPTO_CIPHER_AES_128_GCM,       /* AES-128-GCM (AEAD, 带标签) */
    HWRUN_CRYPTO_CIPHER_AES_256_GCM,       /* AES-256-GCM (AEAD, 带标签) */
} hw_crypto_cipher_t;

/* ============================================================
 * 哈希算法标识
 * ============================================================ */
typedef enum {
    HWRUN_CRYPTO_HASH_SHA256 = 0,
    HWRUN_CRYPTO_HASH_SHA512,
    HWRUN_CRYPTO_HASH_MD5,                 /* 弱算法，仅兼容场景显式使用 */
} hw_crypto_hash_t;

/* ============================================================
 * 非对称算法标识
 * ============================================================ */
typedef enum {
    HWRUN_CRYPTO_ASYM_RSA_2048 = 0,
    HWRUN_CRYPTO_ASYM_RSA_4096,
} hw_crypto_asym_t;

/* 默认信息摘要算法名，用于 RSA 签名/验签 */
#define HW_CRYPTO_RSA_DEFAULT_MD   "sha256"
#define HW_CRYPTO_GCM_TAG_LEN      16          /* GCM 认证标签长度 */
#define HW_CRYPTO_MAX_KEY_RSA      8192        /* RSA 最大位宽 */

/* ============================================================
 * 对称加解密请求/响应
 * ============================================================ */
typedef struct hw_crypto_sym_req {
    int             cipher;              /* hw_crypto_cipher_t */
    const uint8_t  *key;   uint32_t key_len;
    const uint8_t  *iv;    uint32_t iv_len;
    const uint8_t  *aad;   uint32_t aad_len;   /* 仅 AEAD/GCM 使用 */
    const uint8_t  *in;    uint32_t in_len;    /* encrypt: 明文; decrypt: 密文 */
    uint8_t        *out;   uint32_t out_cap;   /* encrypt: >= in_len + 16; decrypt: >= in_len */
    uint8_t        *tag;   uint32_t tag_cap;   /* GCM 标签缓冲, >= 16; 非 GCM 可传 NULL */
} hw_crypto_sym_req_t;

typedef struct hw_crypto_sym_resp {
    uint32_t out_len;                      /* 实际输出字节数 */
    uint32_t tag_len;                      /* GCM 实际标签字节数 */
} hw_crypto_sym_resp_t;

/* ============================================================
 * 哈希 / HMAC 请求
 * ============================================================ */
typedef struct hw_crypto_hash_req {
    int             algo;                /* hw_crypto_hash_t */
    const uint8_t  *data;   uint32_t data_len;
    uint8_t        *out;    uint32_t out_cap;   /* 摘要缓冲 */
} hw_crypto_hash_req_t;

typedef struct hw_crypto_hash_resp {
    uint32_t out_len;                      /* 摘要实际字节数 */
} hw_crypto_hash_resp_t;

typedef struct hw_crypto_hmac_req {
    int             algo;                /* hw_crypto_hash_t */
    const uint8_t  *key;   uint32_t key_len;
    const uint8_t  *data;  uint32_t data_len;
    uint8_t        *out;   uint32_t out_cap;
} hw_crypto_hmac_req_t;

typedef struct hw_crypto_hmac_resp {
    uint32_t out_len;
} hw_crypto_hmac_resp_t;

/* ============================================================
 * 密钥管理
 * ============================================================ */
typedef struct hw_crypto_keygen_req {
    int     asym;                /* 1=RSA 私钥/公钥; 0=对称密钥 */
    int     algo;                /* 对称: hw_crypto_cipher_t; 非对称: hw_crypto_asym_t */
    uint32_t bits;               /* RSA 位宽 (2048/4096); 对称时可为 128/256 由 algo 决定 */
} hw_crypto_keygen_req_t;

/* 对称密钥生成结果（密钥材料，供调用方按需持久化） */
typedef struct hw_crypto_symkey_resp {
    uint8_t  key[64];           /* 可容纳 AES-256 */
    uint32_t key_len;
} hw_crypto_symkey_resp_t;

/* RSA 密钥对（PEM 编码） */
typedef struct hw_crypto_rsa_resp {
    char    priv_pem[16 * 1024];   /* PKCS#8 私钥 PEM */
    size_t  priv_len;
    char    pub_pem[4 * 1024];     /* SubjectPublicKeyInfo 公钥 PEM */
    size_t  pub_len;
} hw_crypto_rsa_resp_t;

/* ============================================================
 * RSA 签名 / 验签
 * ============================================================ */
typedef struct hw_crypto_sign_req {
    const char   *priv_pem;    /* PKCS#8 私钥 PEM 字符串 */
    size_t        priv_len;
    const char   *md_name;     /* 摘要名 "sha256" / "sha512"; NULL 取默认 */
    const uint8_t *data; uint32_t data_len;
    uint8_t      *sig;  uint32_t sig_cap;
} hw_crypto_sign_req_t;

typedef struct hw_crypto_sign_resp {
    uint32_t sig_len;
} hw_crypto_sign_resp_t;

typedef struct hw_crypto_verify_req {
    const char   *pub_pem;    /* 公钥 PEM 字符串 */
    size_t        pub_len;
    const char   *md_name;
    const uint8_t *data; uint32_t data_len;
    const uint8_t *sig;  uint32_t sig_len;
} hw_crypto_verify_req_t;

typedef struct hw_crypto_verify_resp {
    int valid;      /* 1=签名有效; 0=无效 */
} hw_crypto_verify_resp_t;

/* ============================================================
 * 算法能力
 * ============================================================ */
typedef struct hw_crypto_capability {
    int cipher_supported[8];
    int hash_supported[8];
    int asym_supported[4];
    int rng_available;
    int libcrypto_available;    /* 1=使用 libcrypto; 0=回退 openssl CLI */
    char backend[64];           /* "libcrypto 3.x" / "openssl cli" */
} hw_crypto_capability_t;

/* ============================================================
 * CRYPTO 协议接口 (get_interface("CRYPTO") 返回此指针)
 * ============================================================ */
typedef struct hw_crypto_ops {
    int32_t (*version)(void);

    /* --- 安全随机数 --- */
    int (*random)(uint8_t *buf, uint32_t len);

    /* --- 哈希 / HMAC --- */
    int (*hash)(const hw_crypto_hash_req_t *req, hw_crypto_hash_resp_t *resp);
    int (*hmac)(const hw_crypto_hmac_req_t *req, hw_crypto_hmac_resp_t *resp);

    /* --- 对称加解密 --- */
    int (*encrypt)(const hw_crypto_sym_req_t *req, hw_crypto_sym_resp_t *resp);
    int (*decrypt)(const hw_crypto_sym_req_t *req, hw_crypto_sym_resp_t *resp);

    /* --- 密钥管理 --- */
    int (*gen_sym_key)(const hw_crypto_keygen_req_t *req,
                       hw_crypto_symkey_resp_t *resp);
    int (*gen_rsa_key)(const hw_crypto_keygen_req_t *req,
                       hw_crypto_rsa_resp_t *resp);
    /* PEM 导入导出：导入校验并返回长度；导出为字符串拷贝 */
    int (*import_pem)(const char *pem, size_t len, int expected_asym, int *is_asym);
    int (*export_pem)(const char *in_pem, size_t in_len, int want_priv,
                      char *out_pem, size_t *out_len);

    /* --- RSA 签名 / 验签 --- */
    int (*sign)(const hw_crypto_sign_req_t *req, hw_crypto_sign_resp_t *resp);
    int (*verify)(const hw_crypto_verify_req_t *req, hw_crypto_verify_resp_t *resp);

    /* --- 能力探测 --- */
    int (*get_capabilities)(hw_crypto_capability_t *cap);
} hw_crypto_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_CRYPTO_H */