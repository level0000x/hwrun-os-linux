/*
 * sp_key.c — SP 密钥管理与数字签名
 *
 * 支持 RSA-2048 / RSA-4096 / ECDSA-P256 密钥对的生成、导入、导出
 * （PEM 内存持有，按自增 key_id 索引），以及基于私钥的 SHA-256
 * 数字签名与公钥验签。
 *
 * 底层调用本机 `openssl`：
 *   - genkey：openssl genpkey [-pkeyopt rsak/r bit 或 EC 曲线]
 *   - 公钥导出：openssl pkey -pubout
 *   - 签名：   openssl dgst -sha256 -sign <priv.pem>
 *   - 验签：   openssl dgst -sha256 -verify <pub.pem>
 * 私钥始终保存在内存 PEM 与临时文件中，操作完成后立即清理。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "sp.h"
#include "sp_internal.h"

/* ============================================================
 * 算法 -> genpkey 参数
 * ============================================================ */
static int sp_key_gen_cmd(int algo, char *cmd, size_t cap,
                          const char *priv, const char *pub) {
    char qp[8192], qr[8192];
    const char *qpriv = sp_q(priv, qp, sizeof(qp));
    const char *qpub  = sp_q(pub, qr, sizeof(qr));

    switch (algo) {
        case SP_ASYM_RSA_2048:
            return snprintf(cmd, cap,
                            "openssl genpkey -quiet -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out %s && "
                            "openssl pkey -in %s -pubout -out %s",
                            qpriv, qpriv, qpub);
        case SP_ASYM_RSA_4096:
            return snprintf(cmd, cap,
                            "openssl genpkey -quiet -algorithm RSA -pkeyopt rsa_keygen_bits:4096 -out %s && "
                            "openssl pkey -in %s -pubout -out %s",
                            qpriv, qpriv, qpub);
        case SP_ASYM_ECDSA_P256:
            return snprintf(cmd, cap,
                            "openssl genpkey -quiet -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out %s && "
                            "openssl pkey -in %s -pubout -out %s",
                            qpriv, qpriv, qpub);
        default:
            return -1;
    }
}

/* ============================================================
 * 存储 PEM 到密钥条目（调用方须持锁）
 * ============================================================ */
static sp_key_entry_t *sp_key_new_entry(int algo, const char *desc,
                                        const char *priv, const char *pub) {
    sp_key_entry_t *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->key_id = g_sp_ctx.next_key_id++;
    e->algo = algo;
    snprintf(e->desc, sizeof(e->desc), "%s", desc ? desc : "");
    e->priv_pem = strdup(priv);
    e->pub_pem  = strdup(pub);
    e->created_ms = sp_now_ms();
    e->revoked = 0;
    if (!e->priv_pem || !e->pub_pem) {
        free(e->priv_pem); free(e->pub_pem); free(e);
        return NULL;
    }
    e->next = g_sp_ctx.keys;
    g_sp_ctx.keys = e;
    return e;
}

/* ============================================================
 * 按 ID 查找未撤销密钥（调用方须持锁）；out 返回条目指针
 * ============================================================ */
static int sp_key_find(uint32_t key_id, sp_key_entry_t **out, int check_revoked) {
    sp_key_entry_t *e;
    for (e = g_sp_ctx.keys; e; e = e->next) {
        if (e->key_id == key_id) {
            if (check_revoked && e->revoked) { *out = e; return SP_EACCES; }
            *out = e;
            return SP_OK;
        }
    }
    *out = NULL;
    return SP_ENOTFOUND;
}

/* ============================================================
 * 密钥对生成
 * ============================================================ */
int sp_gen_key_pair(int algo, const char *desc, uint32_t *key_id) {
    if (!key_id) return SP_EINVAL;
    if (algo != SP_ASYM_RSA_2048 && algo != SP_ASYM_RSA_4096 &&
        algo != SP_ASYM_ECDSA_P256)
        return SP_ENOTSUP;

    char pip[8192], pup[8192];
    char *priv = sp_tmp_path(pip, sizeof(pip));
    char *pub  = sp_tmp_path(pup, sizeof(pup));
    if (!priv || !pub) return SP_EIO;

    char cmd[16384];
    if (sp_key_gen_cmd(algo, cmd, sizeof(cmd), priv, pub) < 0) return SP_ENOTSUP;

    /* genpkey 的进度点阵输出到 stderr，收走避免污染插件输出 */
    char po[8192], pe[8192];
    char *gout = sp_tmp_path(po, sizeof(po));
    char *gerr = sp_tmp_path(pe, sizeof(pe));
    int rc;
    if (gout && gerr) {
        rc = sp_sh(cmd, gout, gerr);
        unlink(gout); unlink(gerr);
    } else {
        rc = sp_sh(cmd, NULL, NULL);
    }
    if (rc != 0) { unlink(priv); unlink(pub); return SP_EIO; }

    char *privStr = malloc(16384), *pubStr = malloc(16384);
    if (!privStr || !pubStr) { free(privStr); free(pubStr); unlink(priv); unlink(pub); return SP_ENOMEM; }
    ssize_t np = sp_read_text(priv, privStr, 16384);
    ssize_t nu = sp_read_text(pub, pubStr, 16384);
    unlink(priv); unlink(pub);
    if (np <= 0 || nu <= 0) { free(privStr); free(pubStr); return SP_EIO; }

    pthread_mutex_lock(&g_sp_ctx.lock);
    sp_key_entry_t *e = sp_key_new_entry(algo, desc, privStr, pubStr);
    pthread_mutex_unlock(&g_sp_ctx.lock);
    free(privStr); free(pubStr);
    if (!e) return SP_ENOMEM;

    *key_id = e->key_id;
    return SP_OK;
}

/* ============================================================
 * 获取公钥 PEM
 * ============================================================ */
int sp_get_public_key(uint32_t key_id, char *pem_out, uint32_t pem_cap) {
    if (!pem_out) return SP_EINVAL;
    sp_key_entry_t *e;
    pthread_mutex_lock(&g_sp_ctx.lock);
    int rc = sp_key_find(key_id, &e, 1);
    if (rc == SP_OK) {
        if (strlen(e->pub_pem) + 1 > pem_cap) { rc = SP_ENOMEM; }
        else { memcpy(pem_out, e->pub_pem, strlen(e->pub_pem) + 1); }
    }
    pthread_mutex_unlock(&g_sp_ctx.lock);
    return rc;
}

/* ============================================================
 * 导出私钥 PEM
 * ============================================================ */
int sp_export_private_key(uint32_t key_id, char *pem_out, uint32_t pem_cap) {
    if (!pem_out) return SP_EINVAL;
    sp_key_entry_t *e;
    pthread_mutex_lock(&g_sp_ctx.lock);
    int rc = sp_key_find(key_id, &e, 0);
    if (rc == SP_OK) {
        if (strlen(e->priv_pem) + 1 > pem_cap) { rc = SP_ENOMEM; }
        else { memcpy(pem_out, e->priv_pem, strlen(e->priv_pem) + 1); }
    }
    pthread_mutex_unlock(&g_sp_ctx.lock);
    return rc;
}

/* ============================================================
 * 导入密钥对（给定私钥 PEM，自动派生公钥并校验）
 * ============================================================ */
int sp_import_key_pair(int algo, const char *desc,
                       const char *priv_pem, uint32_t *key_id) {
    if (!priv_pem || !key_id) return SP_EINVAL;
    if (algo != SP_ASYM_RSA_2048 && algo != SP_ASYM_RSA_4096 &&
        algo != SP_ASYM_ECDSA_P256)
        return SP_ENOTSUP;

    char pip[8192], pup[8192];
    char *priv = sp_tmp_path(pip, sizeof(pip));
    char *pub  = sp_tmp_path(pup, sizeof(pup));
    if (!priv || !pub) return SP_EIO;

    if (sp_write_all(priv, priv_pem, strlen(priv_pem)) != SP_OK) {
        unlink(priv); unlink(pub); return SP_EIO;
    }

    /* 校验并派生公钥 */
    char qi[8192], qo[8192];
    char cmd[16384];
    snprintf(cmd, sizeof(cmd), "openssl pkey -in %s -pubout -out %s",
             sp_q(priv, qi, sizeof(qi)), sp_q(pub, qo, sizeof(qo)));
    int rc = sp_sh(cmd, NULL, NULL);
    if (rc != 0) { unlink(priv); unlink(pub); return SP_EINVAL; }

    char *pubStr = malloc(16384);
    if (!pubStr) { unlink(priv); unlink(pub); return SP_ENOMEM; }
    ssize_t nu = sp_read_text(pub, pubStr, 16384);
    unlink(priv); unlink(pub);
    if (nu <= 0) { free(pubStr); return SP_EIO; }

    pthread_mutex_lock(&g_sp_ctx.lock);
    sp_key_entry_t *e = sp_key_new_entry(algo, desc, priv_pem, pubStr);
    pthread_mutex_unlock(&g_sp_ctx.lock);
    free(pubStr);
    if (!e) return SP_ENOMEM;
    *key_id = e->key_id;
    return SP_OK;
}

/* ============================================================
 * 撤销密钥（置 revoked）
 * ============================================================ */
int sp_revoke_key(uint32_t key_id) {
    sp_key_entry_t *e;
    pthread_mutex_lock(&g_sp_ctx.lock);
    int rc = sp_key_find(key_id, &e, 0);
    if (rc == SP_OK) e->revoked = 1;
    pthread_mutex_unlock(&g_sp_ctx.lock);
    return rc;
}

/* ============================================================
 * 销毁密钥（删除条目）
 * ============================================================ */
int sp_destroy_key(uint32_t key_id) {
    pthread_mutex_lock(&g_sp_ctx.lock);
    sp_key_entry_t **pp = &g_sp_ctx.keys;
    while (*pp) {
        if ((*pp)->key_id == key_id) {
            sp_key_entry_t *t = *pp;
            *pp = t->next;
            free(t->priv_pem); free(t->pub_pem); free(t);
            pthread_mutex_unlock(&g_sp_ctx.lock);
            return SP_OK;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&g_sp_ctx.lock);
    return SP_ENOTFOUND;
}

/* ============================================================
 * 列出密钥
 * ============================================================ */
int sp_list_keys(char *buf, uint32_t buf_cap) {
    if (!buf) return SP_EINVAL;
    size_t used = 0;
    buf[0] = 0;
    pthread_mutex_lock(&g_sp_ctx.lock);
    for (sp_key_entry_t *e = g_sp_ctx.keys; e; e = e->next) {
        int n = snprintf(buf + used, buf_cap - used,
                         "%u\t%s\t%s\t%s\n", e->key_id,
                         e->algo == SP_ASYM_RSA_2048 ? "RSA-2048" :
                         (e->algo == SP_ASYM_RSA_4096 ? "RSA-4096" : "ECDSA-P256"),
                         e->revoked ? "REVOKED" : "ACTIVE", e->desc);
        if (n < 0 || (size_t)n >= buf_cap - used) { pthread_mutex_unlock(&g_sp_ctx.lock); return SP_ENOMEM; }
        used += (size_t)n;
    }
    pthread_mutex_unlock(&g_sp_ctx.lock);
    return SP_OK;
}

/* ============================================================
 * 数字签名（SHA-256，私钥 PKCS#1 v1.5 / ECDSA），输出 base64
 * ============================================================ */
int sp_sign(uint32_t key_id, const uint8_t *data, uint32_t len,
            char *sig_b64_out, uint32_t b64_cap) {
    if (!data || !sig_b64_out) return SP_EINVAL;

    pthread_mutex_lock(&g_sp_ctx.lock);
    sp_key_entry_t *e;
    int rc = sp_key_find(key_id, &e, 1);
    if (rc != SP_OK) { pthread_mutex_unlock(&g_sp_ctx.lock); return rc; }
    char *privPem = strdup(e->priv_pem);
    pthread_mutex_unlock(&g_sp_ctx.lock);
    if (!privPem) return SP_ENOMEM;

    char pip[8192], pkey[8192], psig[8192], qi[8192], qk[8192], qs[8192];
    char *inpath = sp_tmp_path(pip, sizeof(pip));
    char *keypath = sp_tmp_path(pkey, sizeof(pkey));
    char *sigpath = sp_tmp_path(psig, sizeof(psig));
    if (!inpath || !keypath || !sigpath) { free(privPem); return SP_EIO; }
    if (sp_write_all(inpath, data, len) != SP_OK) { free(privPem); return SP_EIO; }
    if (sp_write_all(keypath, privPem, strlen(privPem)) != SP_OK) { free(privPem); return SP_EIO; }
    free(privPem);

    char cmd[16384];
    snprintf(cmd, sizeof(cmd), "openssl dgst -sha256 -sign %s -out %s %s",
             sp_q(keypath, qk, sizeof(qk)), sp_q(sigpath, qs, sizeof(qs)),
             sp_q(inpath, qi, sizeof(qi)));
    rc = sp_sh(cmd, NULL, NULL);
    if (rc != 0) { unlink(inpath); unlink(keypath); unlink(sigpath); return SP_EIO; }

    /* 读取签名二进制，量 4096 内 */
    uint8_t sig[8192];
    ssize_t n = sp_read_text(sigpath, (char *)sig, sizeof(sig));
    unlink(inpath); unlink(keypath); unlink(sigpath);
    if (n <= 0) return SP_EIO;

    int b64rc = sp_b64_encode(sig, (size_t)n, sig_b64_out, b64_cap);
    return b64rc;
}

/* ============================================================
 * 验签（返回 0，*valid 表示签名有效性）
 * ============================================================ */
int sp_verify(uint32_t key_id, const uint8_t *data, uint32_t len,
              const char *sig_b64, int *valid) {
    if (!data || !sig_b64 || !valid) return SP_EINVAL;
    *valid = 0;

    pthread_mutex_lock(&g_sp_ctx.lock);
    sp_key_entry_t *e;
    int rc = sp_key_find(key_id, &e, 1);
    if (rc != SP_OK) { pthread_mutex_unlock(&g_sp_ctx.lock); return rc; }
    char *pubPem = strdup(e->pub_pem);
    pthread_mutex_unlock(&g_sp_ctx.lock);
    if (!pubPem) return SP_ENOMEM;

    char pip[8192], pkey[8192], psig[8192], qi[8192], qk[8192], qs[8192], pout[8192], qerr[8192];
    char *inpath  = sp_tmp_path(pip, sizeof(pip));
    char *keypath = sp_tmp_path(pkey, sizeof(pkey));
    char *sigpath = sp_tmp_path(psig, sizeof(psig));
    char *outpath = sp_tmp_path(pout, sizeof(pout));
    char *errpath = sp_tmp_path(qerr, sizeof(qerr));
    if (!inpath || !keypath || !sigpath || !outpath || !errpath) { free(pubPem); return SP_EIO; }
    if (sp_write_all(inpath, data, len) != SP_OK) { free(pubPem); return SP_EIO; }
    if (sp_write_all(keypath, pubPem, strlen(pubPem)) != SP_OK) { free(pubPem); return SP_EIO; }
    free(pubPem);

    /* decode base64 到签名文件 */
    size_t slen = 0;
    uint8_t sig[8192];
    rc = sp_b64_decode(sig_b64, sig, sizeof(sig), &slen);
    if (rc != SP_OK) {
        unlink(inpath); unlink(keypath); unlink(sigpath); unlink(outpath); unlink(errpath);
        return rc;
    }
    if (sp_write_all(sigpath, sig, slen) != SP_OK) {
        unlink(inpath); unlink(keypath); unlink(sigpath); unlink(outpath); unlink(errpath);
        return SP_EIO;
    }

    char cmd[16384];
    snprintf(cmd, sizeof(cmd), "openssl dgst -sha256 -verify %s -signature %s %s",
             sp_q(keypath, qk, sizeof(qk)), sp_q(sigpath, qs, sizeof(qs)),
             sp_q(inpath, qi, sizeof(qi)));

    /* openssl: 验签成功返回 0，失败返回非 0；stdout/stderr 均收走，避免污染插件输出 */
    int vrc = sp_sh(cmd, outpath, errpath);
    /* 无论验签失败或内部错误，均视为无效签名，不返回下层错误码，避免信息泄露 */
    *valid = (vrc == 0) ? 1 : 0;

    unlink(inpath); unlink(keypath); unlink(sigpath); unlink(outpath); unlink(errpath);
    return SP_OK;
}

/* ============================================================
 * 回调装配
 * ============================================================ */
static int ops_list_keys_impl(char *b, uint32_t c) { return sp_list_keys(b, c); }

void sp_key_bind(hw_sp_ops_t *ops) {
    if (!ops) return;
    ops->gen_key_pair       = sp_gen_key_pair;
    ops->get_public_key     = sp_get_public_key;
    ops->export_private_key = sp_export_private_key;
    ops->import_key_pair    = sp_import_key_pair;
    ops->revoke_key         = sp_revoke_key;
    ops->destroy_key        = sp_destroy_key;
    ops->list_keys          = ops_list_keys_impl;
    ops->sign               = sp_sign;
    ops->verify             = sp_verify;
}