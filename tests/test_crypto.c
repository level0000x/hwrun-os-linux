/*
 * test_crypto.c — CMocka 单元测试：CRYPTO 密码学协议（dlopen .so 后真实调用）
 *
 * 测试对象：crypto/build/crypto.so 插件及其 provides 协议 "CRYPTO" 的
 * hw_crypto_ops_t（真协议路径，非打桩）。CRYPTO 的 requires 声明含 SP，
 * 但 crypto_impl.c 全部操作直接调用 OpenSSL libcrypto（EVP），自包含、
 * 不依赖 SP 密钥服务——因此单独 dlopen crypto.so、不注册 SP 亦可真跑。
 *
 * 链路（与 test_proto.c 的 load_plugin 模式一致）：
 *   dlopen(.so) -> hw_plugin_entry() -> init()
 *   -> metaproto_register(provides, implementation=get_interface("CRYPTO"))
 *   -> start -> metaproto_resolve("CRYPTO") -> hw_crypto_ops_t*
 *
 * 覆盖要点（Linux + OpenSSL 已装，全部真算）：
 *   - random：16 字节随机数，成功且不全为零；
 *   - hash：SHA-256("abc") == 已知向量（ba78...），SHA-512 输出 64 字节；
 *   - hmac：HMAC-SHA256 RFC 4231 Test Case 1 已知向量；
 *   - encrypt/decrypt：AES-256-CBC 与 AES-128-GCM(带 AAD/tag) 往返，
 *     明文 == decrypt(encrypt(明文))；
 *   - gen_sym_key：AES-256 生成 32 字节密钥；
 *   - gen_rsa_key + sign + verify：RSA-2048 真实签名/验签往返；
 *   - get_capabilities：libcrypto 后端在位、算法表已填充。
 *
 * 错误约定：成功返回 HWRUN_OK(0)，失败返回负 errno。
 * 清理：stop -> unregister -> destroy -> dlclose；p 是 .so 静态描述符，
 * 不得 free(p)；插件不挂入 bus->plugins，防 hw_bus_shutdown 二次卸载。
 */

#include "hwrun.h"
#include "bus.h"
#include "metaproto.h"
#include "../crypto/include/crypto.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <dlfcn.h>
#include <cmocka.h>

#ifndef HWRUN_PLUGIN_ROOT
#define HWRUN_PLUGIN_ROOT "../"
#endif

static hw_bus_t g_bus;
static void *g_h = NULL;        /* dlopen 句柄            */
static hw_plugin_t *g_p = NULL; /* .so 静态描述符，不 free */
static hw_crypto_ops_t *g_ops = NULL;

/* SHA-256("abc") 已知向量 */
static const uint8_t SHA256_ABC[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
};

/* HMAC-SHA256 RFC 4231 Test Case 1 已知向量（key=0x0b*20, data="Hi There"） */
static const uint8_t HMAC_TC1_KEY[20] = {
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
};
static const uint8_t HMAC_TC1_EXPECTED[32] = {
    0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
    0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7,
};

/* ---- 组级 setup：总线 + dlopen + 生命周期 + 注册 provides ---- */
static int group_setup(void **state) {
    (void)state;
    if (hw_bus_init(&g_bus, NULL, NULL, HWLOG_WARN) != HWRUN_OK) {
        fprintf(stderr, "test_crypto: hw_bus_init 失败\n");
        return -1;
    }

    char so[512];
    snprintf(so, sizeof(so), HWRUN_PLUGIN_ROOT "crypto/build/crypto.so");
    g_h = dlopen(so, RTLD_NOW | RTLD_GLOBAL);
    if (!g_h) {
        printf("  [skip] dlopen(%s): %s\n", so, dlerror());
        return 0;
    }

    hw_plugin_t *(*entry)(void) = (hw_plugin_t * (*)(void)) dlsym(g_h, "hw_plugin_entry");
    if (!entry) {
        printf("  [skip] %s: 无 hw_plugin_entry\n", so);
        dlclose(g_h);
        g_h = NULL;
        return 0;
    }
    hw_plugin_t *self = entry();
    if (!self) {
        dlclose(g_h);
        g_h = NULL;
        return 0;
    }

    if (self->ops.init) self->ops.init(self);
    self->state = HWPLUGIN_LOADED;

    for (int i = 0; i < self->provides_count; i++) {
        void *impl = self->ops.get_interface ? self->ops.get_interface(self->provides[i]) : NULL;
        hw_metaproto_register(&g_bus.meta, self->provides[i], HWRUN_PROTOCOL_VERSION, self->id,
                              impl ? impl : (void *)self);
    }
    if (self->ops.start) self->ops.start(self);
    self->state = HWPLUGIN_STARTED;
    g_p = self;

    hw_protocol_route_t *r = NULL;
    if (hw_metaproto_resolve(&g_bus.meta, HWPROTO_CRYPTO, NULL, &r) == HWRUN_OK && r &&
        r->implementation)
        g_ops = (hw_crypto_ops_t *)r->implementation;
    else
        printf("  [warn] resolve(CRYPTO) 未取得实现指针\n");
    return 0;
}

/* ---- 组级 teardown：卸载插件（p 不 free） ---- */
static int group_teardown(void **state) {
    (void)state;
    if (g_p) {
        if (g_p->ops.stop) g_p->ops.stop(g_p);
        for (int j = 0; j < g_p->provides_count; j++)
            hw_metaproto_unregister(&g_bus.meta, g_p->provides[j], g_p->id);
        if (g_p->ops.destroy) g_p->ops.destroy(g_p);
    }
    if (g_h) dlclose(g_h);
    g_p = NULL;
    g_h = NULL;
    g_ops = NULL;
    hw_bus_shutdown(&g_bus);
    return 0;
}

/* ============================================================
 * 用例
 * ============================================================ */

/* RNG + 哈希（已知向量）+ HMAC（RFC 4231 TC1 已知向量） */
static void test_crypto_rng_hash_hmac(void **state) {
    (void)state;
    if (!g_ops) {
        printf("  [skip] CRYPTO 不可用\n");
        return;
    }

    /* 安全随机数：16 字节，极大概率非全零 */
    uint8_t rnd[16];
    assert_int_equal(g_ops->random(rnd, sizeof(rnd)), HWRUN_OK);
    int nonzero = 0;
    for (size_t i = 0; i < sizeof(rnd); i++)
        if (rnd[i]) nonzero = 1;
    assert_true(nonzero);

    /* SHA-256("abc") 已知向量，out_len == 32 */
    {
        const char *msg = "abc";
        uint8_t out[64];
        hw_crypto_hash_resp_t resp;
        hw_crypto_hash_req_t req;
        memset(&req, 0, sizeof(req));
        req.algo = HWRUN_CRYPTO_HASH_SHA256;
        req.data = (const uint8_t *)msg;
        req.data_len = (uint32_t)strlen(msg);
        req.out = out;
        req.out_cap = sizeof(out);
        assert_int_equal(g_ops->hash(&req, &resp), HWRUN_OK);
        assert_int_equal(resp.out_len, 32);
        assert_memory_equal(out, SHA256_ABC, 32);
    }

    /* SHA-512 输出固定 64 字节 */
    {
        const char *msg = "HWRun OS crypto hash";
        uint8_t out[64];
        hw_crypto_hash_resp_t resp;
        hw_crypto_hash_req_t req;
        memset(&req, 0, sizeof(req));
        req.algo = HWRUN_CRYPTO_HASH_SHA512;
        req.data = (const uint8_t *)msg;
        req.data_len = (uint32_t)strlen(msg);
        req.out = out;
        req.out_cap = sizeof(out);
        assert_int_equal(g_ops->hash(&req, &resp), HWRUN_OK);
        assert_int_equal(resp.out_len, 64);
    }

    /* HMAC-SHA256 RFC 4231 TC1 已知向量 */
    {
        const char *msg = "Hi There";
        uint8_t out[64];
        hw_crypto_hmac_resp_t resp;
        hw_crypto_hmac_req_t req;
        memset(&req, 0, sizeof(req));
        req.algo = HWRUN_CRYPTO_HASH_SHA256;
        req.key = HMAC_TC1_KEY;
        req.key_len = sizeof(HMAC_TC1_KEY);
        req.data = (const uint8_t *)msg;
        req.data_len = (uint32_t)strlen(msg);
        req.out = out;
        req.out_cap = sizeof(out);
        assert_int_equal(g_ops->hmac(&req, &resp), HWRUN_OK);
        assert_int_equal(resp.out_len, 32);
        assert_memory_equal(out, HMAC_TC1_EXPECTED, 32);
    }
}

/* 对称加解密往返：AES-256-CBC（PKCS7）与 AES-128-GCM（AEAD + AAD/tag） */
static void test_crypto_sym_roundtrip(void **state) {
    (void)state;
    if (!g_ops) {
        printf("  [skip] CRYPTO 不可用\n");
        return;
    }
    const char *pt = "HWRun OS CRYPTO symmetric roundtrip 0123456789";

    /* ---- AES-256-CBC ---- */
    {
        uint8_t key[32], iv[16];
        memset(key, 0x11, sizeof(key));
        memset(iv, 0x22, sizeof(iv));

        uint8_t enc[128], dec[128];
        hw_crypto_sym_resp_t eresp, dresp;
        hw_crypto_sym_req_t req;
        memset(&req, 0, sizeof(req));
        req.cipher = HWRUN_CRYPTO_CIPHER_AES_256_CBC;
        req.key = key;
        req.key_len = sizeof(key);
        req.iv = iv;
        req.iv_len = sizeof(iv);
        req.in = (const uint8_t *)pt;
        req.in_len = (uint32_t)strlen(pt);
        req.out = enc;
        req.out_cap = sizeof(enc);

        assert_int_equal(g_ops->encrypt(&req, &eresp), HWRUN_OK);
        assert_true(eresp.out_len >= (uint32_t)strlen(pt) + 1); /* 有填充 */
        assert_true(eresp.out_len % 16 == 0);

        /* 解密回来必须与明文一致 */
        req.in = enc;
        req.in_len = eresp.out_len;
        req.out = dec;
        req.out_cap = sizeof(dec);
        assert_int_equal(g_ops->decrypt(&req, &dresp), HWRUN_OK);
        assert_int_equal(dresp.out_len, (uint32_t)strlen(pt));
        assert_memory_equal(dec, pt, strlen(pt));
    }

    /* ---- AES-128-GCM（AAD + 认证标签） ---- */
    {
        uint8_t key[16], iv[12], tag[16];
        memset(key, 0x33, sizeof(key));
        memset(iv, 0x44, sizeof(iv));
        const char *aad = "gcm-aad";

        uint8_t enc[128], dec[128];
        hw_crypto_sym_resp_t eresp, dresp;
        hw_crypto_sym_req_t req;
        memset(&req, 0, sizeof(req));
        req.cipher = HWRUN_CRYPTO_CIPHER_AES_128_GCM;
        req.key = key;
        req.key_len = sizeof(key);
        req.iv = iv;
        req.iv_len = sizeof(iv);
        req.aad = (const uint8_t *)aad;
        req.aad_len = (uint32_t)strlen(aad);
        req.in = (const uint8_t *)pt;
        req.in_len = (uint32_t)strlen(pt);
        req.out = enc;
        req.out_cap = sizeof(enc);
        req.tag = tag;
        req.tag_cap = sizeof(tag);

        assert_int_equal(g_ops->encrypt(&req, &eresp), HWRUN_OK);
        assert_int_equal(eresp.out_len, (uint32_t)strlen(pt)); /* AEAD 无填充 */
        assert_int_equal(eresp.tag_len, HW_CRYPTO_GCM_TAG_LEN);

        req.in = enc;
        req.in_len = eresp.out_len;
        req.out = dec;
        req.out_cap = sizeof(dec);
        /* decrypt 需要 tag：重填（encrypt 已写入 req.tag 同缓冲） */
        assert_int_equal(g_ops->decrypt(&req, &dresp), HWRUN_OK);
        assert_int_equal(dresp.out_len, (uint32_t)strlen(pt));
        assert_memory_equal(dec, pt, strlen(pt));
    }
}

/* 对称密钥生成 + RSA 密钥对 + 签名/验签往返 */
static void test_crypto_keys_sign(void **state) {
    (void)state;
    if (!g_ops) {
        printf("  [skip] CRYPTO 不可用\n");
        return;
    }

    /* 对称密钥：AES-256 -> 32 字节 */
    {
        hw_crypto_keygen_req_t kreq;
        hw_crypto_symkey_resp_t kresp;
        memset(&kreq, 0, sizeof(kreq));
        kreq.asym = 0;
        kreq.algo = HWRUN_CRYPTO_CIPHER_AES_256_CBC;
        kreq.bits = 256;
        assert_int_equal(g_ops->gen_sym_key(&kreq, &kresp), HWRUN_OK);
        assert_int_equal(kresp.key_len, 32);
    }

    /* RSA-2048 密钥对 -> sign -> verify */
    {
        hw_crypto_keygen_req_t kreq;
        hw_crypto_rsa_resp_t kresp;
        memset(&kreq, 0, sizeof(kreq));
        memset(&kresp, 0, sizeof(kresp));
        kreq.asym = 1;
        kreq.bits = 2048;
        assert_int_equal(g_ops->gen_rsa_key(&kreq, &kresp), HWRUN_OK);
        assert_true(kresp.priv_len > 0 && kresp.pub_len > 0);
        assert_non_null(strstr(kresp.priv_pem, "PRIVATE KEY"));
        assert_non_null(strstr(kresp.pub_pem, "PUBLIC KEY"));

        const char *data = "HWRun OS crypto RSA sign payload";
        uint8_t sig[512];
        hw_crypto_sign_resp_t sresp;
        hw_crypto_sign_req_t sreq;
        memset(&sreq, 0, sizeof(sreq));
        sreq.priv_pem = kresp.priv_pem;
        sreq.priv_len = kresp.priv_len;
        sreq.md_name = NULL; /* 默认 sha256 */
        sreq.data = (const uint8_t *)data;
        sreq.data_len = (uint32_t)strlen(data);
        sreq.sig = sig;
        sreq.sig_cap = sizeof(sig);
        assert_int_equal(g_ops->sign(&sreq, &sresp), HWRUN_OK);
        assert_true(sresp.sig_len > 0);

        hw_crypto_verify_resp_t vresp;
        hw_crypto_verify_req_t vreq;
        memset(&vreq, 0, sizeof(vreq));
        vreq.pub_pem = kresp.pub_pem;
        vreq.pub_len = kresp.pub_len;
        vreq.md_name = NULL;
        vreq.data = (const uint8_t *)data;
        vreq.data_len = (uint32_t)strlen(data);
        vreq.sig = sig;
        vreq.sig_len = sresp.sig_len;
        assert_int_equal(g_ops->verify(&vreq, &vresp), HWRUN_OK);
        assert_int_equal(vresp.valid, 1);

        /* 篡改数据应验签失败（valid==0 且 rc==0） */
        const char *tampered = "HWRun OS crypto RSA sign payload!";
        vreq.data = (const uint8_t *)tampered;
        vreq.data_len = (uint32_t)strlen(tampered);
        assert_int_equal(g_ops->verify(&vreq, &vresp), HWRUN_OK);
        assert_int_equal(vresp.valid, 0);
    }
}

/* 能力探测：libcrypto 后端在位、算法表已填充 */
static void test_crypto_capabilities(void **state) {
    (void)state;
    if (!g_ops) {
        printf("  [skip] CRYPTO 不可用\n");
        return;
    }
    hw_crypto_capability_t cap;
    memset(&cap, 0, sizeof(cap));
    assert_int_equal(g_ops->get_capabilities(&cap), HWRUN_OK);
    assert_int_equal(cap.libcrypto_available, 1);
    assert_true(cap.backend[0] != '\0');
    assert_int_equal(cap.cipher_supported[0], HWRUN_CRYPTO_CIPHER_AES_128_CBC);
    assert_int_equal(cap.hash_supported[0], HWRUN_CRYPTO_HASH_SHA256);
    assert_int_equal(cap.asym_supported[0], HWRUN_CRYPTO_ASYM_RSA_2048);
    assert_true(g_ops->version() > 0);
    printf("  [info] backend=%s\n", cap.backend);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_crypto_rng_hash_hmac),
        cmocka_unit_test(test_crypto_sym_roundtrip),
        cmocka_unit_test(test_crypto_keys_sign),
        cmocka_unit_test(test_crypto_capabilities),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
