/*
 * sp_hash.c — SP 哈希与完整性校验
 *
 * 提供 SHA-1 / SHA-256 / SHA-512 / SHA3-256 / SHA3-512 等哈希，
 * 以及文件哈希与完整性比对。数据经由临时文件传入 openssl dgst，
 * 输出为二进制摘要。双向接口与 hw_sp_ops_t 一致。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sp.h"
#include "sp_internal.h"

/* ============================================================
 * 算法 -> openssl dgst 标志 / 输出长度
 * ============================================================ */
static const char *sp_hash_flag(int algo) {
    switch (algo) {
    case SP_HASH_SHA1:
        return "sha1";
    case SP_HASH_SHA256:
        return "sha256";
    case SP_HASH_SHA512:
        return "sha512";
    case SP_HASH_SHA3_256:
        return "sha3-256";
    case SP_HASH_SHA3_512:
        return "sha3-512";
    default:
        return NULL;
    }
}

static int sp_hash_outlen(int algo) {
    switch (algo) {
    case SP_HASH_SHA1:
        return 20;
    case SP_HASH_SHA256:
        return 32;
    case SP_HASH_SHA512:
        return 64;
    case SP_HASH_SHA3_256:
        return 32;
    case SP_HASH_SHA3_512:
        return 64;
    default:
        return 0;
    }
}

/* ============================================================
 * 核心：对已存在文件计算哈希。
 *
 * 通过 `openssl dgst -<alg> -r <file>` 输出统一格式 "hex *path"，
 * 捕获 stdout 后解析首个十六进制令牌（避免 openssl -binary 重定向
 * 在本环境的不可靠行为）。
 * ============================================================ */
static int sp_hash_bin_file(int algo, const char *path, uint8_t *out, uint32_t cap,
                            uint32_t *out_len) {
    const char *flag = sp_hash_flag(algo);
    int want = sp_hash_outlen(algo);
    if (!flag || want <= 0 || !path || !out) return SP_EINVAL;
    if (cap < (uint32_t)want) return SP_ENOMEM;

    char pout[8192], qbuf[8192];
    char *outpath = sp_tmp_path(pout, sizeof(pout));
    if (!outpath) return SP_EIO;

    char cmd[16384];
    const char *qq = sp_q(path, qbuf, sizeof(qbuf));
    snprintf(cmd, sizeof(cmd), "openssl dgst -%s -r %s", flag, qq);

    int rc = sp_sh(cmd, outpath, NULL);
    if (rc != 0) {
        unlink(outpath);
        return SP_EIO;
    }

    char hex[256];
    ssize_t n = sp_read_text(outpath, hex, sizeof(hex));
    unlink(outpath);
    if (n <= 0) return SP_EIO;

    /* 取首个空白前的十六进制令牌 */
    char *tok = hex;
    char *stop = hex;
    while (*stop && *stop != ' ' && *stop != '\t' && *stop != '\n')
        stop++;
    *stop = 0;
    size_t hlen = (size_t)(stop - tok);
    if (hlen != (size_t)want * 2u) return SP_EIO;

    size_t blen = 0;
    rc = sp_from_hex(tok, out, cap, &blen);
    if (rc != SP_OK) return rc;
    if (blen != (size_t)want) return SP_EIO;
    if (out_len) *out_len = (uint32_t)blen;
    return SP_OK;
}

/* ============================================================
 * 对内存数据计算哈希（写入临时文件）
 * ============================================================ */
int sp_hash_compute(int algo, const uint8_t *data, uint32_t len, uint8_t *out, uint32_t *out_len) {
    if (!data || len == 0) return SP_EINVAL;

    char pip[8192];
    char *inpath = sp_tmp_path(pip, sizeof(pip));
    if (!inpath) return SP_EIO;
    if (sp_write_all(inpath, data, len) != SP_OK) return SP_EIO;

    int rc = sp_hash_bin_file(algo, inpath, out, out_len ? *out_len : SP_MAX_HASH_LEN, out_len);
    unlink(inpath);
    return rc;
}

/* 对已存在文件计算哈希 */
int sp_hash_file(int algo, const char *path, uint8_t *out, uint32_t *out_len) {
    return sp_hash_bin_file(algo, path, out, *out_len, out_len);
}

/* ============================================================
 * 以下为 hw_sp_ops_t 回调实现
 * ============================================================ */

/* 返回十六进制摘要字符串（内存数据） */
static int ops_hash_hex(int algo, const uint8_t *data, uint32_t len, char *out_hex,
                        uint32_t hex_cap) {
    uint8_t digest[SP_MAX_HASH_LEN];
    uint32_t dlen = sizeof(digest);
    int rc = sp_hash_compute(algo, data, len, digest, &dlen);
    if (rc != SP_OK) return rc;
    if (hex_cap < dlen * 2u + 1u) return SP_ENOMEM;
    sp_to_hex(digest, dlen, out_hex);
    return SP_OK;
}

/* 返回二进制摘要（内存数据） */
static int ops_hash(int algo, const uint8_t *data, uint32_t len, uint8_t *out, uint32_t *out_len) {
    return sp_hash_compute(algo, data, len, out, out_len);
}

/* 文件哈希（二进制） */
static int ops_hash_file(int algo, const char *path, uint8_t *out, uint32_t *out_len) {
    return sp_hash_bin_file(algo, path, out, *out_len, out_len);
}

/* 文件哈希（十六进制） */
static int ops_hash_file_hex(int algo, const char *path, char *out_hex, uint32_t hex_cap) {
    uint8_t digest[SP_MAX_HASH_LEN];
    uint32_t dlen = sizeof(digest);
    int rc = sp_hash_bin_file(algo, path, digest, sizeof(digest), &dlen);
    if (rc != SP_OK) return rc;
    if (hex_cap < dlen * 2u + 1u) return SP_ENOMEM;
    sp_to_hex(digest, dlen, out_hex);
    return SP_OK;
}

/* 校验内存数据哈希是否匹配预期 */
static int ops_verify_hash(int algo, const uint8_t *data, uint32_t len, const uint8_t *expect,
                           uint32_t expect_len) {
    uint8_t digest[SP_MAX_HASH_LEN];
    uint32_t dlen = sizeof(digest);
    int rc = sp_hash_compute(algo, data, len, digest, &dlen);
    if (rc != SP_OK) return rc;
    if (expect_len != dlen) return SP_EAUTH;
    return sp_ct_eq(digest, expect, dlen) == 0 ? SP_OK : SP_EAUTH;
}

/* 校验文件哈希是否匹配预期的十六进制串 */
static int ops_verify_file_hash(int algo, const char *path, const char *expected_hex) {
    if (!path || !expected_hex) return SP_EINVAL;
    uint8_t digest[SP_MAX_HASH_LEN];
    uint32_t dlen = sizeof(digest);
    int rc = sp_hash_bin_file(algo, path, digest, sizeof(digest), &dlen);
    if (rc != SP_OK) return rc;

    uint8_t expect[SP_MAX_HASH_LEN];
    size_t elen = 0;
    if (sp_from_hex(expected_hex, expect, sizeof(expect), &elen) != SP_OK) return SP_EINVAL;
    if (elen != dlen) return SP_EAUTH;
    return sp_ct_eq(digest, expect, dlen) == 0 ? SP_OK : SP_EAUTH;
}

/* 供主入口装配哈希回调 */
void sp_hash_bind(hw_sp_ops_t *ops) {
    if (!ops) return;
    ops->hash = ops_hash;
    ops->hash_hex = ops_hash_hex;
    ops->hash_file = ops_hash_file;
    ops->hash_file_hex = ops_hash_file_hex;
    ops->verify_hash = ops_verify_hash;
    ops->verify_file_hash = ops_verify_file_hash;
}