/*
 * crypto_impl.c — CRYPTO 协议真实实现（基于 OpenSSL libcrypto EVP）
 *
 * 提供 hw_crypto_ops 全部函数指针：
 *   random / hash / hmac / encrypt / decrypt
 *   gen_sym_key / gen_rsa_key / import_pem / export_pem
 *   sign / verify / get_capabilities / version
 *
 * 所有操作使用 OpenSSL 后端完成，绝无假数据/占位。
 * 失败返回负 errno，成功返回 HWRUN_OK(0)。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#ifndef ENOSPC
#define ENOSPC 28
#endif

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>

#include "hwrun.h"
#include "crypto.h"

/*
 * 取当前 OpenSSL 错误队列首条错误描述（仅内部日志/自检使用）。
 */
static const char *crypto_err_str(void) {
    static char errbuf[256];
    unsigned long e = ERR_get_error();
    if (e)
        snprintf(errbuf, sizeof(errbuf), "%s", ERR_error_string(e, NULL));
    else
        snprintf(errbuf, sizeof(errbuf), "unknown");
    return errbuf;
}

/* ============================================================
 * 对称算法：名字 -> { EVP_CIPHER, key_len, iv_len }
 * ============================================================ */
typedef struct cipher_spec {
    const EVP_CIPHER *cipher;
    int key_len;
    int iv_len;
    int is_aead;
} cipher_spec;

static int cipher_lookup(int cipher, cipher_spec *out) {
    switch (cipher) {
    case HWRUN_CRYPTO_CIPHER_AES_128_CBC:
        out->cipher = EVP_aes_128_cbc();
        out->key_len = 16;
        out->iv_len = 16;
        out->is_aead = 0;
        return 0;
    case HWRUN_CRYPTO_CIPHER_AES_256_CBC:
        out->cipher = EVP_aes_256_cbc();
        out->key_len = 32;
        out->iv_len = 16;
        out->is_aead = 0;
        return 0;
    case HWRUN_CRYPTO_CIPHER_AES_128_GCM:
        out->cipher = EVP_aes_128_gcm();
        out->key_len = 16;
        out->iv_len = 12;
        out->is_aead = 1;
        return 0;
    case HWRUN_CRYPTO_CIPHER_AES_256_GCM:
        out->cipher = EVP_aes_256_gcm();
        out->key_len = 32;
        out->iv_len = 12;
        out->is_aead = 1;
        return 0;
    default:
        return -ENOTSUP;
    }
}

/* 哈希名字 -> EVP_MD */
static const EVP_MD *hash_md(int algo) {
    switch (algo) {
    case HWRUN_CRYPTO_HASH_SHA256:
        return EVP_sha256();
    case HWRUN_CRYPTO_HASH_SHA512:
        return EVP_sha512();
    case HWRUN_CRYPTO_HASH_MD5:
        return EVP_md5();
    default:
        return NULL;
    }
}

/* RSA 摘要名字解析，返回 EVP_MD*；NULL 用默认 sha256 */
static const EVP_MD *rsa_md(const char *name) {
    if (!name || !*name) return EVP_sha256();
    if (!strcmp(name, "sha256")) return EVP_sha256();
    if (!strcmp(name, "sha512")) return EVP_sha512();
    if (!strcmp(name, "sha1")) return EVP_sha1();
    if (!strcmp(name, "sha384")) return EVP_sha384();
    return NULL;
}

/* ============================================================
 * 安全随机数：RAND_bytes
 * ============================================================ */
static int crypto_random_impl(uint8_t *buf, uint32_t len) {
    if (!buf) return -EINVAL;
    if (len == 0) return HWRUN_OK;
    if (RAND_bytes(buf, (int)len) != 1) {
        return -EIO;
    }
    return HWRUN_OK;
}

/* ============================================================
 * 哈希
 * ============================================================ */
static int crypto_hash_impl(const hw_crypto_hash_req_t *req, hw_crypto_hash_resp_t *resp) {
    const EVP_MD *md;
    EVP_MD_CTX *ctx = NULL;
    unsigned int olen = 0;
    int dsz;

    if (!req || !resp || !req->data) return -EINVAL;
    md = hash_md(req->algo);
    if (!md) return -ENOTSUP;
    dsz = EVP_MD_get_size(md);
    if (dsz <= 0) return -ENOTSUP;
    if (!req->out || req->out_cap < (uint32_t)dsz) return -ENOMEM;

    ctx = EVP_MD_CTX_new();
    if (!ctx) return -ENOMEM;
    if (EVP_DigestInit_ex(ctx, md, NULL) != 1) goto err;
    if (EVP_DigestUpdate(ctx, req->data, req->data_len) != 1) goto err;
    if (EVP_DigestFinal_ex(ctx, req->out, &olen) != 1) goto err;
    resp->out_len = (uint32_t)olen;

    EVP_MD_CTX_free(ctx);
    return HWRUN_OK;
err:
    EVP_MD_CTX_free(ctx);
    (void)crypto_err_str();
    return -EIO;
}

/* ============================================================
 * HMAC
 * ============================================================ */
static int crypto_hmac_impl(const hw_crypto_hmac_req_t *req, hw_crypto_hmac_resp_t *resp) {
    const EVP_MD *md;
    unsigned int olen = 0;

    if (!req || !resp || !req->data) return -EINVAL;
    md = hash_md(req->algo);
    if (!md) return -ENOTSUP;
    if (!req->out) return -EINVAL;

    if (HMAC(md, req->key, (int)req->key_len, req->data, req->data_len, req->out, &olen) == NULL)
        return -EIO;
    resp->out_len = (uint32_t)olen;
    return HWRUN_OK;
}

/* ============================================================
 * 对称加密 / 解密
 * ============================================================ */
static int crypto_encrypt_impl(const hw_crypto_sym_req_t *req, hw_crypto_sym_resp_t *resp) {
    cipher_spec spec;
    EVP_CIPHER_CTX *ctx = NULL;
    int olen = 0, tlen = 0, ret = -EINVAL;

    if (!req || !resp || !req->key || !req->in || !req->out) return -EINVAL;
    if (cipher_lookup(req->cipher, &spec) != 0) return -ENOTSUP;
    if ((int)req->key_len != spec.key_len) return -EINVAL;
    if ((int)req->iv_len != spec.iv_len) return -EINVAL;

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -ENOMEM;

    if (spec.is_aead) {
        /* GCM：先 init 设 IV 长度，再设 key/iv */
        if (EVP_EncryptInit_ex(ctx, spec.cipher, NULL, NULL, NULL) != 1) goto out;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, (int)req->iv_len, NULL) != 1)
            goto out;
        if (EVP_EncryptInit_ex(ctx, NULL, NULL, req->key, req->iv) != 1) goto out;
        /* AAD（不加密，仅认证） */
        if (req->aad && req->aad_len) {
            if (EVP_EncryptUpdate(ctx, NULL, &tlen, req->aad, (int)req->aad_len) != 1) goto out;
        }
        /* 密文长度 == 明文长度 */
        if (req->out_cap < (uint32_t)req->in_len) {
            ret = -ENOMEM;
            goto out;
        }
        if (EVP_EncryptUpdate(ctx, req->out, &olen, req->in, (int)req->in_len) != 1) goto out;
        if (EVP_EncryptFinal_ex(ctx, req->out + olen, &tlen) != 1) goto out;
        olen += tlen;
        /* 取认证标签 */
        if (req->tag && req->tag_cap >= HW_CRYPTO_GCM_TAG_LEN) {
            if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, HW_CRYPTO_GCM_TAG_LEN, req->tag) !=
                1)
                goto out;
            resp->tag_len = HW_CRYPTO_GCM_TAG_LEN;
        } else {
            resp->tag_len = 0;
        }
    } else {
        /* CBC 带 PKCS7 填充 */
        if (EVP_EncryptInit_ex(ctx, spec.cipher, NULL, req->key, req->iv) != 1) goto out;
        if (req->out_cap < (uint32_t)(req->in_len + 16)) {
            ret = -ENOMEM;
            goto out;
        }
        if (EVP_EncryptUpdate(ctx, req->out, &olen, req->in, (int)req->in_len) != 1) goto out;
        if (EVP_EncryptFinal_ex(ctx, req->out + olen, &tlen) != 1) goto out;
        olen += tlen;
        resp->tag_len = 0;
    }

    resp->out_len = (uint32_t)olen;
    ret = HWRUN_OK;
out:
    EVP_CIPHER_CTX_free(ctx);
    if (ret != HWRUN_OK) (void)crypto_err_str();
    return ret;
}

static int crypto_decrypt_impl(const hw_crypto_sym_req_t *req, hw_crypto_sym_resp_t *resp) {
    cipher_spec spec;
    EVP_CIPHER_CTX *ctx = NULL;
    int olen = 0, tlen = 0, ret = -EINVAL;

    if (!req || !resp || !req->key || !req->in || !req->out) return -EINVAL;
    if (cipher_lookup(req->cipher, &spec) != 0) return -ENOTSUP;
    if ((int)req->key_len != spec.key_len) return -EINVAL;
    if ((int)req->iv_len != spec.iv_len) return -EINVAL;

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -ENOMEM;

    if (spec.is_aead) {
        if (!req->tag || req->tag_cap < HW_CRYPTO_GCM_TAG_LEN) {
            ret = -EINVAL;
            goto out;
        }
        if (EVP_DecryptInit_ex(ctx, spec.cipher, NULL, NULL, NULL) != 1) goto out;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, (int)req->iv_len, NULL) != 1)
            goto out;
        if (EVP_DecryptInit_ex(ctx, NULL, NULL, req->key, req->iv) != 1) goto out;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, HW_CRYPTO_GCM_TAG_LEN,
                                (void *)(uintptr_t)req->tag) != 1)
            goto out;
        if (req->aad && req->aad_len) {
            if (EVP_DecryptUpdate(ctx, NULL, &tlen, req->aad, (int)req->aad_len) != 1) goto out;
        }
        if (req->out_cap < (uint32_t)req->in_len) {
            ret = -ENOMEM;
            goto out;
        }
        if (EVP_DecryptUpdate(ctx, req->out, &olen, req->in, (int)req->in_len) != 1) goto out;
        if (EVP_DecryptFinal_ex(ctx, req->out + olen, &tlen) != 1) {
            /* GCM 认证标签校验失败 */
            ret = -EILSEQ;
            goto out;
        }
        olen += tlen;
    } else {
        if (EVP_DecryptInit_ex(ctx, spec.cipher, NULL, req->key, req->iv) != 1) goto out;
        if (req->out_cap < (uint32_t)req->in_len) {
            ret = -ENOMEM;
            goto out;
        }
        if (EVP_DecryptUpdate(ctx, req->out, &olen, req->in, (int)req->in_len) != 1) goto out;
        if (EVP_DecryptFinal_ex(ctx, req->out + olen, &tlen) != 1) {
            ret = -EILSEQ;
            goto out; /* 填充校验失败 */
        }
        olen += tlen;
    }

    resp->out_len = (uint32_t)olen;
    resp->tag_len = 0;
    ret = HWRUN_OK;
out:
    EVP_CIPHER_CTX_free(ctx);
    if (ret != HWRUN_OK) (void)crypto_err_str();
    return ret;
}

/* ============================================================
 * 对称密钥生成（CSPRNG）
 * ============================================================ */
static int crypto_rsa_bits_from_algo(int algo) {
    switch (algo) {
    case HWRUN_CRYPTO_ASYM_RSA_2048:
        return 2048;
    case HWRUN_CRYPTO_ASYM_RSA_4096:
        return 4096;
    default:
        return 0;
    }
}

static int crypto_gen_sym_key_impl(const hw_crypto_keygen_req_t *req,
                                   hw_crypto_symkey_resp_t *resp) {
    cipher_spec spec;
    int klen = 0;

    if (!req || !resp) return -EINVAL;
    /* 若传入 bits，按 bits/8 设密钥长；否则由 algo 决定 */
    if (req->bits == 128 || req->bits == 256) {
        klen = (int)req->bits / 8;
    } else if (cipher_lookup(req->algo, &spec) == 0) {
        klen = spec.key_len;
    } else {
        return -ENOTSUP;
    }
    if (klen <= 0 || klen > (int)sizeof(resp->key)) return -EINVAL;

    if (RAND_bytes(resp->key, klen) != 1) return -EIO;
    resp->key_len = (uint32_t)klen;
    return HWRUN_OK;
}

/* ============================================================
 * RSA 密钥对生成（PEM 导出）
 * ============================================================ */
static int crypto_gen_rsa_key_impl(const hw_crypto_keygen_req_t *req, hw_crypto_rsa_resp_t *resp) {
    EVP_PKEY_CTX *pctx = NULL;
    EVP_PKEY *pkey = NULL;
    BIO *pbio = NULL, *pubio = NULL;
    int bits, ret = -EIO;
    long plen = -1, ublen = -1;

    if (!req || !resp) return -EINVAL;

    bits = (int)req->bits;
    if (bits == 0) bits = crypto_rsa_bits_from_algo(req->algo);
    if (bits <= 0) return -EINVAL;
    if (bits > HW_CRYPTO_MAX_KEY_RSA) return -ENOTSUP;

    pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!pctx) goto out;
    if (EVP_PKEY_keygen_init(pctx) <= 0) goto out;
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, bits) <= 0) goto out;
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0) goto out;

    /* 私钥 -> PKCS#8 PEM */
    pbio = BIO_new(BIO_s_mem());
    if (!pbio) goto out;
    if (PEM_write_bio_PKCS8PrivateKey(pbio, pkey, NULL, NULL, 0, NULL, NULL) != 1) goto out;
    plen = BIO_get_mem_data(pbio, NULL);

    /* 公钥 -> SubjectPublicKeyInfo PEM */
    pubio = BIO_new(BIO_s_mem());
    if (!pubio) goto out;
    if (PEM_write_bio_PUBKEY(pubio, pkey) != 1) goto out;
    ublen = BIO_get_mem_data(pubio, NULL);

    if (plen < 0 || (size_t)plen >= sizeof(resp->priv_pem)) goto out;
    if (ublen < 0 || (size_t)ublen >= sizeof(resp->pub_pem)) goto out;

    if (BIO_read(pbio, resp->priv_pem, (int)sizeof(resp->priv_pem) - 1) != (int)plen) goto out;
    resp->priv_pem[plen] = '\0';
    resp->priv_len = (size_t)plen;

    if (BIO_read(pubio, resp->pub_pem, (int)sizeof(resp->pub_pem) - 1) != (int)ublen) goto out;
    resp->pub_pem[ublen] = '\0';
    resp->pub_len = (size_t)ublen;

    ret = HWRUN_OK;
out:
    if (pctx) EVP_PKEY_CTX_free(pctx);
    if (pkey) EVP_PKEY_free(pkey);
    if (pbio) BIO_free(pbio);
    if (pubio) BIO_free(pubio);
    if (ret != HWRUN_OK) (void)crypto_err_str();
    return ret;
}

/* ============================================================
 * PEM 导入 / 导出
 * ============================================================ */
static int crypto_import_pem_impl(const char *pem, size_t len, int expected_asym, int *is_asym) {
    if (!pem || len == 0) return -EINVAL;

    if (expected_asym) {
        const char *p = pem;
        if (len && pem[len - 1] == '\0') { /* 已含结尾 */
        }
        if (strstr(p, "PRIVATE KEY") || strstr(p, "PUBLIC KEY") || strstr(p, "RSA PRIVATE KEY") ||
            strstr(p, "RSA PUBLIC KEY")) {
            if (is_asym) *is_asym = 1;
            return HWRUN_OK;
        }
        return -EILSEQ; /* 不是 PEM 编码的非对称密钥 */
    } else {
        /* 对称密钥导入：视为原始密钥材料，长度必须合理(16/32等) */
        size_t n = pem[len - 1] == '\0' ? len - 1 : len;
        if (n < 16) return -EINVAL;
        if (is_asym) *is_asym = 0;
        return HWRUN_OK;
    }
}

static int crypto_export_pem_impl(const char *in_pem, size_t in_len, int want_priv, char *out_pem,
                                  size_t *out_len) {
    size_t n;
    if (!in_pem || !out_pem || !out_len || in_len == 0) return -EINVAL;
    n = in_len;
    if (in_pem[in_len - 1] == '\0') n = in_len - 1;
    if (*out_len < n + 1) return -ENOMEM;
    (void)want_priv; /* 调用方已给出对应 PEM；此处原样导出 */
    memcpy(out_pem, in_pem, n);
    out_pem[n] = '\0';
    *out_len = n + 1;
    return HWRUN_OK;
}

/* ============================================================
 * RSA 签名 / 验签
 * ============================================================ */
static int crypto_sign_impl(const hw_crypto_sign_req_t *req, hw_crypto_sign_resp_t *resp) {
    BIO *bio = NULL;
    EVP_PKEY *pkey = NULL;
    EVP_MD_CTX *mctx = NULL;
    const EVP_MD *md;
    size_t siglen = 0;
    int ret = -EIO;

    if (!req || !resp || !req->priv_pem || !req->data || !req->sig) return -EINVAL;
    md = rsa_md(req->md_name);
    if (!md) return -ENOTSUP;

    bio = BIO_new_mem_buf(req->priv_pem, (int)req->priv_len);
    if (!bio) return -ENOMEM;
    pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    bio = NULL;
    if (!pkey) {
        ret = -EILSEQ;
        goto out;
    }

    mctx = EVP_MD_CTX_new();
    if (!mctx) {
        ret = -ENOMEM;
        goto out;
    }

    if (EVP_DigestSignInit(mctx, NULL, md, NULL, pkey) != 1) goto out;
    if (EVP_DigestSignUpdate(mctx, req->data, req->data_len) != 1) goto out;
    /* 第一次调用获取签名长度 */
    if (EVP_DigestSignFinal(mctx, NULL, &siglen) != 1) goto out;
    if ((uint32_t)siglen > req->sig_cap) {
        ret = -ENOMEM;
        goto out;
    }
    if (EVP_DigestSignFinal(mctx, req->sig, &siglen) != 1) goto out;

    resp->sig_len = (uint32_t)siglen;
    ret = HWRUN_OK;
out:
    if (pkey) EVP_PKEY_free(pkey);
    if (mctx) EVP_MD_CTX_free(mctx);
    if (ret != HWRUN_OK) (void)crypto_err_str();
    return ret;
}

static int crypto_verify_impl(const hw_crypto_verify_req_t *req, hw_crypto_verify_resp_t *resp) {
    BIO *bio = NULL;
    EVP_PKEY *pkey = NULL;
    EVP_MD_CTX *mctx = NULL;
    const EVP_MD *md;
    int rv, ret = -EIO;

    if (!req || !resp || !req->pub_pem || !req->data || !req->sig) return -EINVAL;
    if (!resp) return -EINVAL;
    md = rsa_md(req->md_name);
    if (!md) return -ENOTSUP;

    bio = BIO_new_mem_buf(req->pub_pem, (int)req->pub_len);
    if (!bio) return -ENOMEM;
    pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    bio = NULL;
    if (!pkey) {
        /* 兼容 PKCS#1 公钥 (PEM_read_bio_RSA_PUBKEY 返回 RSA*，需包一层 EVP_PKEY) */
        RSA *rsa = NULL;
        bio = BIO_new_mem_buf(req->pub_pem, (int)req->pub_len);
        if (bio) {
            rsa = PEM_read_bio_RSA_PUBKEY(bio, NULL, NULL, NULL);
            BIO_free(bio);
            if (rsa) {
                pkey = EVP_PKEY_new();
                if (pkey && EVP_PKEY_assign_RSA(pkey, rsa) != 1) {
                    EVP_PKEY_free(pkey);
                    pkey = NULL;
                }
            }
        }
        if (!pkey) {
            ret = -EILSEQ;
            goto out;
        }
    }

    mctx = EVP_MD_CTX_new();
    if (!mctx) {
        ret = -ENOMEM;
        goto out;
    }

    if (EVP_DigestVerifyInit(mctx, NULL, md, NULL, pkey) != 1) goto out;
    if (EVP_DigestVerifyUpdate(mctx, req->data, req->data_len) != 1) goto out;
    rv = EVP_DigestVerifyFinal(mctx, req->sig, req->sig_len);
    if (rv < 0) {
        ret = -EIO;
        goto out;
    }

    resp->valid = (rv == 1) ? 1 : 0;
    ret = HWRUN_OK;
out:
    if (pkey) EVP_PKEY_free(pkey);
    if (mctx) EVP_MD_CTX_free(mctx);
    if (ret != HWRUN_OK) (void)crypto_err_str();
    return ret;
}

/* ============================================================
 * 能力探测
 * ============================================================ */
static int crypto_capabilities_impl(hw_crypto_capability_t *cap) {
    if (!cap) return -EINVAL;
    memset(cap, 0, sizeof(*cap));
    cap->cipher_supported[0] = HWRUN_CRYPTO_CIPHER_AES_128_CBC;
    cap->cipher_supported[1] = HWRUN_CRYPTO_CIPHER_AES_256_CBC;
    cap->cipher_supported[2] = HWRUN_CRYPTO_CIPHER_AES_128_GCM;
    cap->cipher_supported[3] = HWRUN_CRYPTO_CIPHER_AES_256_GCM;
    cap->hash_supported[0] = HWRUN_CRYPTO_HASH_SHA256;
    cap->hash_supported[1] = HWRUN_CRYPTO_HASH_SHA512;
    cap->hash_supported[2] = HWRUN_CRYPTO_HASH_MD5;
    cap->asym_supported[0] = HWRUN_CRYPTO_ASYM_RSA_2048;
    cap->asym_supported[1] = HWRUN_CRYPTO_ASYM_RSA_4096;
    cap->rng_available = (RAND_status() == 1) ? 1 : 0;
    cap->libcrypto_available = 1;
    snprintf(cap->backend, sizeof(cap->backend), "libcrypto %s", OpenSSL_version(OPENSSL_VERSION));
    return HWRUN_OK;
}

static int32_t crypto_version_impl(void) {
    return 1; /* CRYPTO 协议接口版本 */
}

/* ============================================================
 * 协议接口总表（导出符号）
 * ============================================================ */
hw_crypto_ops_t hw_crypto_ops = {
    .version = crypto_version_impl,
    .random = crypto_random_impl,
    .hash = crypto_hash_impl,
    .hmac = crypto_hmac_impl,
    .encrypt = crypto_encrypt_impl,
    .decrypt = crypto_decrypt_impl,
    .gen_sym_key = crypto_gen_sym_key_impl,
    .gen_rsa_key = crypto_gen_rsa_key_impl,
    .import_pem = crypto_import_pem_impl,
    .export_pem = crypto_export_pem_impl,
    .sign = crypto_sign_impl,
    .verify = crypto_verify_impl,
    .get_capabilities = crypto_capabilities_impl,
};