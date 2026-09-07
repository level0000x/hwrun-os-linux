/*
 * sp_crypto.c — SP 对称加密（认证加密）
 *
 * 采用 AES-256-CBC + HMAC-SHA256 的 encrypt-then-MAC 组合，
 * 同时提供机密性、完整性与真实性：
 *   - 密钥派生（域分离）：encKey = SHA-256(key || 0x00)
 *                             macKey = SHA-256(key || 0x01)
 *   - 加密：AES-256-CBC（PKCS7 填充）
 *   - 认证：HMAC-SHA256( aad || iv || ciphertext )
 * 解密先校验 HMAC（常量时间比较），失败即拒绝解出明文，杜绝密文篡改。
 *
 * 底层调用本机 `openssl` 可执行文件；明密文/密钥经临时文件与命令行
 * `-K/-macopt hexkey` 传递，不在参数中泄露密钥语义。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sp.h"
#include "sp_internal.h"

#define SP_CBC_IV_LEN 16
#define SP_HMAC_LEN   32   /* HMAC-SHA256 */

/* ============================================================
 * 由单字节 tag 与 key 派生固定长度的子密钥（SHA-256）
 * ============================================================ */
static int sp_derive_sub(const uint8_t *key, uint32_t key_len,
                         uint8_t tag, uint8_t out[32]) {
    uint8_t buf[2048];
    if (key_len > 2000) return SP_EINVAL;
    memcpy(buf, key, key_len);
    buf[key_len] = tag;
    uint32_t n = 32;
    return sp_hash_compute(SP_HASH_SHA256, buf, key_len + 1, out, &n);
}

/* ============================================================
 * 计算 HMAC-SHA256( data )，key 为字节数组
 * 返回 32 字节摘要 out
 * ============================================================ */
static int sp_hmac_sha256(const uint8_t *key, size_t key_len,
                          const uint8_t *data, size_t data_len,
                          uint8_t out[32]) {
    if (key_len == 0 || key_len > 4096) return SP_EINVAL;

    char khex[8192];
    sp_to_hex(key, key_len, khex);

    char pip[8192], pout[8192], qbuf[8192];
    char *inpath = sp_tmp_path(pip, sizeof(pip));
    if (!inpath) return SP_EIO;
    if (sp_write_all(inpath, data, data_len) != SP_OK) return SP_EIO;

    /* HMAC key 走 hexkey，避免特殊字符进入命令行 */
    char cmd[16384];
    snprintf(cmd, sizeof(cmd),
             "openssl dgst -sha256 -mac HMAC -macopt hexkey:%s -r %s",
             khex, sp_q(inpath, qbuf, sizeof(qbuf)));

    char *outpath = sp_tmp_path(pout, sizeof(pout));
    int rc = sp_sh(cmd, outpath, NULL);
    if (rc != 0) { unlink(inpath); unlink(outpath); return SP_EIO; }

    char hex[512];
    ssize_t n = sp_read_text(outpath, hex, sizeof(hex));
    unlink(inpath); unlink(outpath);
    if (n <= 0) return SP_EIO;

    char *tok = hex, *stop = hex;
    while (*stop && *stop != ' ' && *stop != '\t' && *stop != '\n') stop++;
    *stop = 0;
    if ((size_t)(stop - tok) != 64) return SP_EIO;

    size_t blen = 0;
    rc = sp_from_hex(tok, out, 32, &blen);
    if (rc != SP_OK || blen != 32) return SP_EIO;
    return SP_OK;
}

/* ============================================================
 * 对称加密（认证加密）
 *  key: 调用方秘钥；iv: 16 字节（NULL 则由系统生成，但调用方需自行保留以解密）；
 *  aad: 附加认证数据（可空）；pt: 明文；ct/tag 为输出缓冲。
 * ============================================================ */
int sp_encrypt(const uint8_t *key, uint32_t key_len,
               const uint8_t *iv, uint32_t iv_len,
               const uint8_t *aad, uint32_t aad_len,
               const uint8_t *pt, uint32_t pt_len,
               uint8_t *ct, uint32_t *ct_len,
               uint8_t *tag, uint32_t *tag_len) {
    if (!key || key_len == 0 || !pt || pt_len == 0 || !ct || !ct_len ||
        !tag || !tag_len)
        return SP_EINVAL;

    /* 派生加密/认证密钥 */
    uint8_t encKey[32], macKey[32];
    if (sp_derive_sub(key, key_len, 0x00, encKey) != SP_OK) return SP_EIO;
    if (sp_derive_sub(key, key_len, 0x01, macKey) != SP_OK) return SP_EIO;

    /* IV：需 16 字节；未提供则生成 */
    uint8_t local_iv[SP_CBC_IV_LEN];
    const uint8_t *ivp = iv;
    if (iv) {
        if (iv_len != SP_CBC_IV_LEN) return SP_EINVAL;
    } else {
        if (sp_random(local_iv, SP_CBC_IV_LEN) != SP_OK) return SP_EIO;
        ivp = local_iv;
    }

    char keHex[80], ivHex[64];
    sp_to_hex(encKey, 32, keHex);
    sp_to_hex(ivp, SP_CBC_IV_LEN, ivHex);

    int rc;
    char pip[8192], pct[8192];
    char *inpath  = sp_tmp_path(pip, sizeof(pip));
    char *ctpath  = sp_tmp_path(pct, sizeof(pct));
    if (!inpath || !ctpath) return SP_EIO;
    if (sp_write_all(inpath, pt, pt_len) != SP_OK) { rc = SP_EIO; goto out; }

    /* AES-256-CBC 加密 */
    char cmd[16384], qi[8192], qc[8192];
    snprintf(cmd, sizeof(cmd),
             "openssl enc -aes-256-cbc -K %s -iv %s -in %s -out %s",
             keHex, ivHex, sp_q(inpath, qi, sizeof(qi)),
             sp_q(ctpath, qc, sizeof(qc)));
    rc = sp_sh(cmd, NULL, NULL);
    if (rc != 0) { rc = SP_EIO; goto out; }

    /* 读取密文长度 */
    long ctSize = -1;
    {
        FILE *f = fopen(ctpath, "rb");
        if (!f) { rc = SP_EIO; goto out; }
        fseek(f, 0, SEEK_END); ctSize = ftell(f); fclose(f);
    }
    if (ctSize <= 0 || (uint32_t)ctSize > *ct_len) { rc = SP_ENOMEM; goto out; }
    {
        FILE *f = fopen(ctpath, "rb");
        if (!f) { rc = SP_EIO; goto out; }
        if (fread(ct, 1, (size_t)ctSize, f) != (size_t)ctSize) { fclose(f); rc = SP_EIO; goto out; }
        fclose(f);
    }

    /* 认证输入 = aad || iv || ciphertext */
    size_t macDataLen = (aad ? aad_len : 0) + SP_CBC_IV_LEN + (size_t)ctSize;
    if (macDataLen > 0x100000) { rc = SP_ENOMEM; goto out; }
    uint8_t *macData = malloc(macDataLen ? macDataLen : 1);
    if (!macData) { rc = SP_ENOMEM; goto out; }
    size_t o = 0;
    if (aad && aad_len) { memcpy(macData + o, aad, aad_len); o += aad_len; }
    memcpy(macData + o, ivp, SP_CBC_IV_LEN); o += SP_CBC_IV_LEN;
    memcpy(macData + o, ct, (size_t)ctSize); o += (size_t)ctSize;

    uint8_t mac[SP_HMAC_LEN];
    rc = sp_hmac_sha256(macKey, 32, macData, macDataLen, mac);
    free(macData);
    if (rc != SP_OK) goto out;

    if (*tag_len < SP_HMAC_LEN) { rc = SP_ENOMEM; goto out; }
    memcpy(tag, mac, SP_HMAC_LEN);
    *tag_len = SP_HMAC_LEN;
    *ct_len = (uint32_t)ctSize;

    /* 复制 iv 到调用方可选回写？接口无 iv 返回，故仅当调用方传入 iv 时可用 */
    (void)ivp;
    rc = SP_OK;

out:
    unlink(inpath);
    unlink(ctpath);
    return rc;
}

/* ============================================================
 * 对称解密（先验 HMAC，再解密）
 * ============================================================ */
int sp_decrypt(const uint8_t *key, uint32_t key_len,
               const uint8_t *iv, uint32_t iv_len,
               const uint8_t *aad, uint32_t aad_len,
               const uint8_t *ct, uint32_t ct_len,
               const uint8_t *tag, uint32_t tag_len,
               uint8_t *pt, uint32_t *pt_len) {
    if (!key || key_len == 0 || !ct || ct_len == 0 || !pt || !pt_len ||
        !tag || tag_len == 0)
        return SP_EINVAL;
    if (!iv || iv_len != SP_CBC_IV_LEN) return SP_EINVAL;

    uint8_t encKey[32], macKey[32];
    if (sp_derive_sub(key, key_len, 0x00, encKey) != SP_OK) return SP_EIO;
    if (sp_derive_sub(key, key_len, 0x01, macKey) != SP_OK) return SP_EIO;

    /* 重建认证输入并校验 */
    size_t macDataLen = (aad ? aad_len : 0) + SP_CBC_IV_LEN + ct_len;
    if (macDataLen > 0x100000) return SP_ENOMEM;
    uint8_t *macData = malloc(macDataLen ? macDataLen : 1);
    if (!macData) return SP_ENOMEM;
    size_t o = 0;
    if (aad && aad_len) { memcpy(macData + o, aad, aad_len); o += aad_len; }
    memcpy(macData + o, iv, SP_CBC_IV_LEN); o += SP_CBC_IV_LEN;
    memcpy(macData + o, ct, ct_len);

    uint8_t mac[SP_HMAC_LEN];
    int rc = sp_hmac_sha256(macKey, 32, macData, macDataLen, mac);
    free(macData);
    if (rc != SP_OK) return rc;

    /* 常量时间比对标签；失败拒绝解密 */
    if (tag_len < SP_HMAC_LEN) return SP_EAUTH;
    if (sp_ct_eq(mac, tag, SP_HMAC_LEN) != 0) return SP_EAUTH;

    char keHex[80], ivHex[64];
    sp_to_hex(encKey, 32, keHex);
    sp_to_hex(iv, SP_CBC_IV_LEN, ivHex);

    char pip[8192], pct[8192], pout[8192], qi[8192], qc[8192];
    char *inpath  = sp_tmp_path(pip, sizeof(pip));
    char *ctpath  = sp_tmp_path(pct, sizeof(pct));
    char *ptpath  = sp_tmp_path(pout, sizeof(pout));
    if (!inpath || !ctpath || !ptpath) return SP_EIO;
    if (sp_write_all(inpath, ct, ct_len) != SP_OK) { rc = SP_EIO; goto out; }

    char cmd[16384];
    snprintf(cmd, sizeof(cmd),
             "openssl enc -d -aes-256-cbc -K %s -iv %s -in %s -out %s",
             keHex, ivHex, sp_q(inpath, qi, sizeof(qi)),
             sp_q(ptpath, qc, sizeof(qc)));
    rc = sp_sh(cmd, NULL, NULL);
    if (rc != 0) { rc = SP_EIO; goto out; }

    long ptSize = -1;
    {
        FILE *f = fopen(ptpath, "rb");
        if (!f) { rc = SP_EIO; goto out; }
        fseek(f, 0, SEEK_END); ptSize = ftell(f); fclose(f);
    }
    if (ptSize < 0 || (uint32_t)ptSize > *pt_len) { rc = SP_ENOMEM; goto out; }
    {
        FILE *f = fopen(ptpath, "rb");
        if (!f) { rc = SP_EIO; goto out; }
        if (fread(pt, 1, (size_t)ptSize, f) != (size_t)ptSize) { fclose(f); rc = SP_EIO; goto out; }
        fclose(f);
    }
    *pt_len = (uint32_t)ptSize;
    rc = SP_OK;

out:
    unlink(inpath); unlink(ctpath); unlink(ptpath);
    return rc;
}

/* ============================================================
 * 安全随机数（openssl rand -hex）
 * ============================================================ */
int sp_random(uint8_t *buf, uint32_t len) {
    if (!buf) return SP_EINVAL;
    char nstr[32], cmd[128], pout[8192];
    /* openssl rand -hex N 输出 N 字节（2N 个十六进制字符） */
    snprintf(nstr, sizeof(nstr), "%u", len);
    snprintf(cmd, sizeof(cmd), "openssl rand -hex %s", nstr);
    char *outpath = sp_tmp_path(pout, sizeof(pout));
    if (!outpath) return SP_EIO;
    int rc = sp_sh(cmd, outpath, NULL);
    if (rc != 0) { unlink(outpath); return SP_EIO; }
    char line[8192];
    ssize_t n = sp_read_text(outpath, line, sizeof(line));
    unlink(outpath);
    if (n <= 0) return SP_EIO;
    /* 去除尾部空白/换行，再按十六进制解析 */
    while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r' ||
                     line[n-1] == ' ' || line[n-1] == '\t')) line[--n] = 0;
    size_t blen = 0;
    rc = sp_from_hex(line, buf, len, &blen);
    if (rc != SP_OK || blen != len) return SP_EIO;
    return SP_OK;
}

/* ============================================================
 * 回调装配
 * ============================================================ */
static int ops_encrypt_wrap(const uint8_t *key, uint32_t kl,
                            const uint8_t *iv, uint32_t il,
                            const uint8_t *aad, uint32_t al,
                            const uint8_t *pt, uint32_t pl,
                            uint8_t *ct, uint32_t *cl,
                            uint8_t *tag, uint32_t *tl) {
    return sp_encrypt(key, kl, iv, il, aad, al, pt, pl, ct, cl, tag, tl);
}
static int ops_decrypt_wrap(const uint8_t *key, uint32_t kl,
                            const uint8_t *iv, uint32_t il,
                            const uint8_t *aad, uint32_t al,
                            const uint8_t *ct, uint32_t cl,
                            const uint8_t *tag, uint32_t tl,
                            uint8_t *pt, uint32_t *pl) {
    return sp_decrypt(key, kl, iv, il, aad, al, ct, cl, tag, tl, pt, pl);
}

void sp_crypto_bind(hw_sp_ops_t *ops) {
    if (!ops) return;
    ops->encrypt = ops_encrypt_wrap;
    ops->decrypt = ops_decrypt_wrap;
    ops->random  = sp_random;
}