/*
 * sp.c — HWRun OS 安全协议（SP）插件主入口
 *
 * 实现 hw_plugin_t 生命周期（init/start/stop/destroy），
 * 通过 get_interface("SP") 返回 hw_sp_ops_t 协议接口，
 * 并提供 sp_selftest() 自检（哈希/加密/认证/密钥/签名）。
 *
 * 本插件作为独立 .so 提供给总线 dlopen 加载，导出 hw_plugin_entry()。
 */

#include "hwrun.h"

#include <pthread.h>
#include <dlfcn.h>

#include "../include/sp.h"
#include "sp_internal.h"

/* ---- 协议接口聚合（各模块回调装配） ---- */
static hw_sp_ops_t g_sp_ops;

/* 插件私有上下文 */
typedef struct sp_pdata {
    int started;
} sp_pdata_t;

/* ============================================================
 * 生命周期
 * ============================================================ */
static int sp_init(hw_plugin_t *self) {
    sp_pdata_t *pd;
    if (!self) return HWRUN_EINVAL;

    pd = calloc(1, sizeof(sp_pdata_t));
    if (!pd) return HWRUN_ENOMEM;

    /* 初始化全局上下文 */
    memset(&g_sp_ctx, 0, sizeof(g_sp_ctx));
    g_sp_ctx.initialized = 0;
    g_sp_ctx.next_key_id = 1;
    pthread_mutex_init(&g_sp_ctx.lock, NULL);

    /* 装配协议接口 */
    memset(&g_sp_ops, 0, sizeof(g_sp_ops));
    sp_hash_bind(&g_sp_ops);
    sp_crypto_bind(&g_sp_ops);
    sp_key_bind(&g_sp_ops);
    sp_auth_bind(&g_sp_ops);
    g_sp_ops.selftest = sp_selftest;

    pd->started = 0;
    self->private_data = pd;
    return HWRUN_OK;
}

static int sp_start(hw_plugin_t *self) {
    sp_pdata_t *pd = self ? (sp_pdata_t *)self->private_data : NULL;
    if (!pd) return HWRUN_ENOTREADY;
    if (pd->started) return HWRUN_OK;

    /* 加载用户表（找不到即空表，不报错） */
    sp_auth_init();

    g_sp_ctx.initialized = 1;
    pd->started = 1;
    self->state = HWPLUGIN_STARTED;
    return HWRUN_OK;
}

static int sp_stop(hw_plugin_t *self) {
    sp_pdata_t *pd = self ? (sp_pdata_t *)self->private_data : NULL;
    (void)pd;
    /* 保留用户表，仅停服务 */
    return HWRUN_OK;
}

static int sp_destroy(hw_plugin_t *self) {
    sp_pdata_t *pd = self ? (sp_pdata_t *)self->private_data : NULL;
    sp_auth_cleanup();
    sp_keys_cleanup();
    pthread_mutex_destroy(&g_sp_ctx.lock);
    if (pd) free(pd);
    if (self) self->private_data = NULL;
    return HWRUN_OK;
}

/* 参数变更回调：本插件暂无可配置参数，一律接受 */
static int sp_configure(hw_plugin_t *self, const char *key, const char *value) {
    (void)self; (void)key; (void)value;
    return HWRUN_OK;
}

/* get_interface：返回对外提供的协议实现 */
static void *sp_get_interface(const char *protocol) {
    if (protocol && strcmp(protocol, HWPROTO_SP) == 0) {
        return &g_sp_ops;
    }
    return NULL;
}

/* ============================================================
 * 释放密钥存储
 * ============================================================ */
void sp_keys_cleanup(void) {
    sp_key_entry_t *e = g_sp_ctx.keys;
    g_sp_ctx.keys = NULL;
    while (e) {
        sp_key_entry_t *t = e; e = e->next;
        free(t->priv_pem); free(t->pub_pem); free(t);
    }
}

/* ============================================================
 * 自检：真实执行哈希/加密/认证/密钥/签名，返回 0 全部通过
 * ============================================================ */
int sp_selftest(void) {
    int rc = 0;
    char log[1024];
    int pass = 0, fail = 0;

#define CHECK(expr, what) do { \
        rc = (expr); \
        if (rc != SP_OK) { fprintf(stderr, "SELFTEST FAIL: %s (rc=%d)\n", (what), rc); fail++; } \
        else { pass++; } \
    } while (0)

    /* 1. 哈希与完整性 */
    {
        const char *msg = "HWRun SP selftest message";
        uint8_t d[SP_MAX_HASH_LEN]; uint32_t dl = sizeof(d);
        CHECK(sp_hash_compute(SP_HASH_SHA256, (const uint8_t *)msg, (uint32_t)strlen(msg),
                              d, &dl), "sha256 hash");

        char hex[160];
        CHECK(g_sp_ops.hash_hex(SP_HASH_SHA256, (const uint8_t *)msg,
                                (uint32_t)strlen(msg), hex, sizeof(hex)),
              "sha256 hash_hex");
        /* 已知 SHA256("abc") = ba7816bf... */
        {
            uint8_t v[32]; uint32_t vl = sizeof(v);
            CHECK(sp_hash_compute(SP_HASH_SHA256, (const uint8_t *)"abc", 3, v, &vl),
                  "sha256 abc");
            char h[80]; sp_to_hex(v, 32, h);
            if (strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")!=0) {
                fprintf(stderr, "SELFTEST FAIL: sha256(abc) mismatch\n"); fail++;
            } else pass++;
        }
        /* 完整性不符应返回 EAUTH */
        {
            const char *msg2 = "tampered";
            uint8_t e32[32]; uint32_t el = 32;
            sp_hash_compute(SP_HASH_SHA256, (const uint8_t *)"original", 8, e32, &el);
            if (g_sp_ops.verify_hash(SP_HASH_SHA256, (const uint8_t *)msg2,
                                     (uint32_t)strlen(msg2), e32, el) == SP_EAUTH) pass++;
            else { fprintf(stderr, "SELFTEST FAIL: tamper must be rejected\n"); fail++; }
        }
    }

    /* 2. 对称加解密（认证加密） */
    {
        uint8_t key[32]; sp_random(key, 32);
        uint8_t iv[16];  sp_random(iv, 16);
        const char *pt = "classified payload: transfer 4096 bytes securely";
        uint8_t ct[2048], tag[64], out[2048];
        uint32_t ctlen = sizeof(ct), taglen = sizeof(tag), outlen = sizeof(out);

        CHECK(sp_encrypt(key, 32, iv, 16, (const uint8_t *)"aad", 3,
                         (const uint8_t *)pt, (uint32_t)strlen(pt),
                         ct, &ctlen, tag, &taglen), "aes encrypt");
        CHECK(sp_decrypt(key, 32, iv, 16, (const uint8_t *)"aad", 3,
                         ct, ctlen, tag, taglen, out, &outlen), "aes decrypt");
        if (outlen != (uint32_t)strlen(pt) || memcmp(out, pt, outlen) != 0) {
            fprintf(stderr, "SELFTEST FAIL: decrypt plaintext mismatch\n"); fail++;
        } else pass++;

        /* 篡改密文应被拒绝 */
        uint8_t badct[2048]; memcpy(badct, ct, ctlen);
        badct[2] ^= 0x40;
        uint32_t bol = sizeof(out);
        if (sp_decrypt(key, 32, iv, 16, (const uint8_t *)"aad", 3,
                       badct, ctlen, tag, taglen, out, &bol) == SP_EAUTH) pass++;
        else { fprintf(stderr, "SELFTEST FAIL: tampered ct must trigger EAUTH\n"); fail++; }

        /* AAD 不符应被拒绝 */
        uint32_t aol = sizeof(out);
        if (sp_decrypt(key, 32, iv, 16, (const uint8_t *)"aaa", 3,
                       ct, ctlen, tag, taglen, out, &aol) == SP_EAUTH) pass++;
        else { fprintf(stderr, "SELFTEST FAIL: bad aad must trigger EAUTH\n"); fail++; }
    }

    /* 3. 认证与授权 */
    {
        char sid[SP_SESSION_ID_LEN];
        if (sp_authenticate("alice", "wrong", sid, sizeof(sid)) == SP_EBADCRED) pass++;
        else { fprintf(stderr, "SELFTEST FAIL: wrong password must fail\n"); fail++; }

        CHECK(sp_add_user("alice", "hwrun#2026", "operator", 1), "add_user");
        CHECK(sp_authenticate("alice", "hwrun#2026", sid, sizeof(sid)), "authenticate");
        if (sp_authenticate("alice", "hwrun#2026", sid, sizeof(sid)) == SP_OK &&
            strncmp(sid, "sess-", 5) == 0) pass++;
        else fail++;

        /* operator 可 fs.read / task.submit，不可 config.write */
        if (sp_authorize("alice", "fs.read", "/etc/hwrun") == SP_OK) pass++;
        else fail++;
        if (sp_authorize("alice", "config.write", "") != SP_OK) pass++;
        else { fprintf(stderr, "SELFTEST FAIL: operator must be denied config.write\n"); fail++; }

        /* 未知用户应被拒 */
        if (sp_authorize("nobody", "fs.read", "") != SP_OK) pass++;
        else fail++;

        CHECK(sp_del_user("alice"), "del_user");
    }

    /* 4. 密钥管理 + 签名验签 */
    {
        uint32_t kid = 0;
        CHECK(sp_gen_key_pair(SP_ASYM_RSA_2048, "selftest-key", &kid), "gen rsa-2048");

        char pub[8192];
        CHECK(sp_get_public_key(kid, pub, sizeof(pub)), "get pubkey");
        if (!strstr(pub, "BEGIN PUBLIC KEY")) {
            fprintf(stderr, "SELFTEST FAIL: pubkey PEM invalid\n"); fail++;
        } else pass++;

        const uint8_t data[] = "signature test data";
        char sig[8192];
        CHECK(sp_sign(kid, data, (uint32_t)sizeof(data) - 1, sig, sizeof(sig)), "sign");
        int valid = 0;
        CHECK(sp_verify(kid, data, (uint32_t)sizeof(data) - 1, sig, &valid), "verify");
        if (valid) pass++;
        else { fprintf(stderr, "SELFTEST FAIL: signature should verify\n"); fail++; }

        /* 篡改数据验签应无效 */
        const uint8_t bad[] = "signature test datX";
        sp_verify(kid, bad, sizeof(bad) - 1, sig, &valid);
        if (!valid) pass++;
        else { fprintf(stderr, "SELFTEST FAIL: tampered data must fail verify\n"); fail++; }

        CHECK(sp_revoke_key(kid), "revoke");
        CHECK(sp_destroy_key(kid), "destroy");
    }

#undef CHECK

    snprintf(log, sizeof(log), "SP selftest: %d passed, %d failed", pass, fail);
    fprintf(stderr, "%s\n", log);
    return (fail == 0) ? SP_OK : SP_EIO;
}

/* ============================================================
 * 插件入口：dlopen 加载时调用，返回 hw_plugin_t*
 * ============================================================ */
static hw_plugin_t g_sp_plugin;

__attribute__((visibility("default")))
hw_plugin_t *hw_plugin_entry(void) {
    memset(&g_sp_plugin, 0, sizeof(g_sp_plugin));

    /* 基础元数据（与 plugin.yml 保持一致） */
    strncpy(g_sp_plugin.id, "sp", sizeof(g_sp_plugin.id) - 1);
    strncpy(g_sp_plugin.name, "Security Protocol",
            sizeof(g_sp_plugin.name) - 1);
    strncpy(g_sp_plugin.version, "1.0.0", sizeof(g_sp_plugin.version) - 1);
    g_sp_plugin.type = HWPLUGIN_TYPE_SECURITY;
    g_sp_plugin.state = HWPLUGIN_INSTALLED;
    strncpy(g_sp_plugin.description,
            "HWRun OS 安全协议：认证、授权、加密、解密、哈希、签名、密钥管理与完整性校验",
            sizeof(g_sp_plugin.description) - 1);

    /* 提供的协议 */
    static char *sp_provides[] = { "SP" };
    g_sp_plugin.provides = sp_provides;
    g_sp_plugin.provides_count = 1;

    /* 依赖的协议（依赖链第 7 环；仅声明，真正校验由总线 && METAPROTO 完成） */
    static char *sp_requires[] = { "LOG", "PARAM", "HAP", "PMP", "FSP", "METAPROTO" };
    g_sp_plugin.requires = sp_requires;
    g_sp_plugin.requires_count = 6;

    /* 生命周期 */
    g_sp_plugin.ops.init           = sp_init;
    g_sp_plugin.ops.start          = sp_start;
    g_sp_plugin.ops.stop           = sp_stop;
    g_sp_plugin.ops.destroy        = sp_destroy;
    g_sp_plugin.ops.configure      = sp_configure;
    g_sp_plugin.ops.get_interface  = sp_get_interface;

    return &g_sp_plugin;
}