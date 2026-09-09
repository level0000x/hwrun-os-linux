/*
 * test_compress.c — CMocka 单元测试：COMPRESS 压缩协议（dlopen .so 后真实调用）
 *
 * 测试对象：compress/build/compress.so 插件及其 provides 协议 "COMPRESS" 的
 * hw_compress_ops_t（真压缩库 zlib+libzstd，非打桩）。COMPRESS 的 requires 含
 * LOG/PARAM/METAPROTO，但实现自包含（直接调系统压缩库），故单独 dlopen 亦可真跑。
 *
 * 链路（与 test_crypto.c 一致）：
 *   dlopen(.so) -> hw_plugin_entry() -> init()
 *   -> metaproto_register(provides, implementation=get_interface("COMPRESS"))
 *   -> start -> metaproto_resolve("COMPRESS") -> hw_compress_ops_t*
 *
 * 覆盖要点（全部真压缩/解压）：
 *   - list_algorithms：算法表含 zlib/gzip/zstd（supported=1）与 xz/lz4（仅探测）；
 *   - detect：对压缩产物按魔数识别为对应算法；
 *   - buffer：zstd/gzip/zlib 三种 200KB 数据压缩+自动识别解压往返 == 原文；
 *   - 未知/损坏输入解压失败；xz 压缩返回 -ENOTSUP（v1 未链接）；
 *   - file：zstd 文件压缩+自动识别解压往返 == 原文；keep_original=0 删原文件。
 *
 * 错误约定：成功返回 0，失败返回负 errno。result.data 由 ops.free_result 释放。
 * 清理：stop -> unregister -> destroy -> dlclose；p 是 .so 静态描述符不得 free。
 */

#include "hwrun.h"
#include "bus.h"
#include "metaproto.h"
#include "../compress/include/compress.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <setjmp.h>
#include <dlfcn.h>
#include <cmocka.h>

#ifndef HWRUN_PLUGIN_ROOT
#define HWRUN_PLUGIN_ROOT "../"
#endif

static hw_bus_t g_bus;
static void *g_h = NULL; /* dlopen 句柄 */
static hw_plugin_t *g_p = NULL;
static hw_compress_ops_t *g_ops = NULL;

/* ---- 组级 setup：总线 + dlopen + 生命周期 + 注册 provides ---- */
static int group_setup(void **state) {
    (void)state;
    if (hw_bus_init(&g_bus, NULL, NULL, HWLOG_WARN) != HWRUN_OK) {
        fprintf(stderr, "test_compress: hw_bus_init 失败\n");
        return -1;
    }

    char so[512];
    snprintf(so, sizeof(so), HWRUN_PLUGIN_ROOT "compress/build/compress.so");
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
    if (hw_metaproto_resolve(&g_bus.meta, HWPROTO_COMPRESS, NULL, &r) == HWRUN_OK && r &&
        r->implementation)
        g_ops = (hw_compress_ops_t *)r->implementation;
    else
        printf("  [warn] resolve(COMPRESS) 未取得实现指针\n");
    return 0;
}

static int group_teardown(void **state) {
    (void)state;
    if (!g_p) return 0;
    if (g_p->ops.stop) g_p->ops.stop(g_p);
    for (int i = 0; i < g_p->provides_count; i++)
        hw_metaproto_unregister(&g_bus.meta, g_p->provides[i], g_p->id);
    if (g_p->ops.destroy) g_p->ops.destroy(g_p);
    dlclose(g_h);
    g_h = NULL;
    g_p = NULL;
    g_ops = NULL;
    hw_bus_shutdown(&g_bus);
    return 0;
}

/* ---- 200KB 可压缩样本 ---- */
static uint8_t *make_sample(uint32_t *len_out) {
    uint32_t n = 200 * 1024;
    uint8_t *buf = (uint8_t *)malloc(n);
    assert_non_null(buf);
    static const char pat[] = "HWRun OS COMPRESS protocol sample data 0123456789; ";
    for (uint32_t i = 0; i < n; i++)
        buf[i] = (uint8_t)pat[i % (sizeof(pat) - 1)];
    *len_out = n;
    return buf;
}

static void test_compress_algorithms(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    hw_compress_info_t info[8];
    memset(info, 0, sizeof(info));
    int count = 0;
    assert_int_equal(g_ops->list_algorithms(info, 8, &count), 0);
    assert_int_equal(count, 6);
    int zstd_supported = 0, xz_supported = 1;
    for (int i = 0; i < count; i++) {
        if (info[i].algo == HW_COMPRESS_ALGO_ZSTD) zstd_supported = info[i].supported;
        if (info[i].algo == HW_COMPRESS_ALGO_XZ) xz_supported = info[i].supported;
    }
    assert_int_equal(zstd_supported, 1);
    assert_int_equal(xz_supported, 0); /* v1 仅探测 */
    printf("  [info] list_algorithms: %d entries, zstd supported, xz detect-only\n", count);
}

static void test_compress_roundtrip_one(int algo, const char *tag) {
    uint32_t n = 0;
    uint8_t *plain = make_sample(&n);

    hw_compress_result_t c;
    memset(&c, 0, sizeof(c));
    int rc = g_ops->compress(algo, HW_COMPRESS_LEVEL_DEFAULT, plain, n, &c);
    assert_int_equal(rc, 0);
    assert_non_null(c.data);
    assert_true(c.data_len < n);
    assert_int_equal(c.algo, algo);

    /* 魔数识别 */
    int d_algo = HW_COMPRESS_ALGO_AUTO;
    assert_int_equal(g_ops->detect(c.data, c.data_len, &d_algo), 0);
    assert_int_equal(d_algo, algo);
    printf("  [info] %s: %u -> %u bytes\n", tag, n, c.data_len);

    hw_compress_result_t d;
    memset(&d, 0, sizeof(d));
    rc = g_ops->decompress(HW_COMPRESS_ALGO_AUTO, c.data, c.data_len, &d);
    assert_int_equal(rc, 0);
    assert_int_equal(d.data_len, n);
    assert_memory_equal(d.data, plain, n);
    assert_int_equal(d.algo, algo);

    g_ops->free_result(&c);
    g_ops->free_result(&d);
    free(plain);
}

static void test_compress_buffer_roundtrip(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    test_compress_roundtrip_one(HW_COMPRESS_ALGO_ZSTD, "zstd");
    test_compress_roundtrip_one(HW_COMPRESS_ALGO_GZIP, "gzip");
    test_compress_roundtrip_one(HW_COMPRESS_ALGO_ZLIB, "zlib");
}

static void test_compress_bad_input(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    /* 未压缩/垃圾输入：自动识别解压应失败 */
    uint8_t garbage[64];
    for (int i = 0; i < 64; i++)
        garbage[i] = (uint8_t)(i * 7 + 3);
    hw_compress_result_t r;
    memset(&r, 0, sizeof(r));
    assert_int_not_equal(g_ops->decompress(HW_COMPRESS_ALGO_AUTO, garbage, sizeof(garbage), &r), 0);
    /* xz/lz4 压缩：v1 未链接 → ENOTSUP */
    assert_int_equal(
        g_ops->compress(HW_COMPRESS_ALGO_XZ, HW_COMPRESS_LEVEL_DEFAULT, garbage, 16, &r), -ENOTSUP);
    assert_int_equal(
        g_ops->compress(HW_COMPRESS_ALGO_LZ4, HW_COMPRESS_LEVEL_DEFAULT, garbage, 16, &r),
        -ENOTSUP);
}

static void test_compress_file_roundtrip(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    char in[160], zst[160], out[160], in2[160];
    snprintf(in, sizeof(in), "/tmp/hwcm_in_%d.bin", (int)getpid());
    snprintf(zst, sizeof(zst), "/tmp/hwcm_%d.bin.zst", (int)getpid());
    snprintf(out, sizeof(out), "/tmp/hwcm_out_%d.bin", (int)getpid());
    snprintf(in2, sizeof(in2), "/tmp/hwcm_del_%d.bin", (int)getpid());

    uint32_t n = 0;
    uint8_t *plain = make_sample(&n);
    FILE *f = fopen(in, "wb");
    assert_non_null(f);
    assert_int_equal(fwrite(plain, 1, n, f), n);
    fclose(f);
    /* 同内容第二份用于删除语义验证 */
    FILE *f2 = fopen(in2, "wb");
    assert_non_null(f2);
    assert_int_equal(fwrite(plain, 1, n, f2), n);
    fclose(f2);

    hw_compress_file_req_t req;
    hw_compress_file_resp_t resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.input_path = in;
    req.output_path = zst;
    req.algo = HW_COMPRESS_ALGO_ZSTD;
    req.level = HW_COMPRESS_LEVEL_DEFAULT;
    req.keep_original = 1;
    assert_int_equal(g_ops->compress_file(&req, &resp), 0);
    assert_true(resp.result_len < resp.orig_len);
    assert_int_equal(resp.algo, HW_COMPRESS_ALGO_ZSTD);

    /* 自动识别解压 */
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.input_path = zst;
    req.output_path = out;
    req.algo = HW_COMPRESS_ALGO_AUTO;
    req.keep_original = 1;
    assert_int_equal(g_ops->decompress_file(&req, &resp), 0);
    assert_int_equal(resp.orig_len, n);

    FILE *chk = fopen(out, "rb");
    assert_non_null(chk);
    uint8_t *got = (uint8_t *)malloc(n);
    assert_non_null(got);
    assert_int_equal(fread(got, 1, n, chk), n);
    fclose(chk);
    assert_memory_equal(got, plain, n);
    free(got);

    /* keep_original=0：成功后删除原文件 */
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    req.input_path = in2;
    req.output_path = zst;
    req.algo = HW_COMPRESS_ALGO_ZSTD;
    req.level = HW_COMPRESS_LEVEL_DEFAULT;
    req.keep_original = 0;
    assert_int_equal(g_ops->compress_file(&req, &resp), 0);
    assert_int_equal(access(in2, F_OK), -1);

    remove(in);
    remove(zst);
    remove(out);
    free(plain);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_compress_algorithms),
        cmocka_unit_test(test_compress_buffer_roundtrip),
        cmocka_unit_test(test_compress_bad_input),
        cmocka_unit_test(test_compress_file_roundtrip),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
