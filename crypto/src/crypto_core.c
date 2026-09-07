/*
 * crypto_core.c — CRYPTO 协议插件主入口与生命周期
 *
 * 实现 hw_plugin_entry()：返回 hw_plugin_t*，其 ops 提供
 * init/start/stop/destroy/configure/get_interface。get_interface("CRYPTO")
 * 返回 hw_crypto_ops_t*（真实实现，基于 OpenSSL libcrypto）。
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include "hwrun.h"
#include "crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 跨平台导出宏：Windows/msys2 PE 需 __declspec(dllexport)；ELF 用 visibility */
#if defined(_WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
#define HW_CRYPTO_EXPORT __declspec(dllexport)
#define HW_CRYPTO_EXPORT_DEF
#else
#define HW_CRYPTO_EXPORT __attribute__((visibility("default")))
#define HW_CRYPTO_EXPORT_DEF
#endif

/* 由实现文件提供（crypto_impl.c） */
extern hw_crypto_ops_t hw_crypto_ops;

/* provProvides/requires 字符串表（与 plugin.yml 保持一致） */
static const char *const crypto_provides_arr[] = { "CRYPTO" };
static const char *const crypto_requires_arr[] = { "SP", "PARAM", "LOG", "METAPROTO" };
static const int        crypto_provides_cnt = 1;
static const int        crypto_requires_cnt = 4;

/* ---- 生命周期 ----
 * CRYPTO 子模块为无状态加密原语宿主，调用时按需分配 EVP 上下文，
 * 故 init/start/stop/destroy 无需持有长期资源，均返回 HWRUN_OK。 */
static int g_plugin_state = HWPLUGIN_LOADED;

static int crypto_plugin_init(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_LOADED;
    return HWRUN_OK;
}

static int crypto_plugin_start(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_STARTED;
    return HWRUN_OK;
}

static int crypto_plugin_stop(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_STOPPED;
    return HWRUN_OK;
}

static int crypto_plugin_destroy(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_UNINSTALLED;
    return HWRUN_OK;
}

/* 参数变更回调：当前无配置项，返回 0 表示接受 */
static int crypto_plugin_configure(hw_plugin_t *self,
                                   const char *key, const char *value) {
    (void)self; (void)key; (void)value;
    return HWRUN_OK;
}

/* get_interface：对外提供 CRYPTO 协议接口指针 */
static void *crypto_plugin_get_interface(const char *protocol) {
    if (!protocol) return NULL;
    if (strcmp(protocol, HWPROTO_CRYPTO) == 0)
        return &hw_crypto_ops;
    return NULL;
}

/* ============================================================
 * 插件描述符（导出）
 * ============================================================ */
static struct hw_plugin hw_crypto_plugin = {
    .id          = "crypto",
    .name        = "Cryptographic Protocol (SP submodule)",
    .version     = "1.0.0",
    .type        = HWPLUGIN_TYPE_CRYPTO,
    .state       = HWPLUGIN_UNINSTALLED,
    .description = "HWRun OS CRYPTO protocol: symmetric/asymmetric encryption, hash, HMAC, sign, key mgmt, RNG",
    .repo_url    = {0},
    .branch      = {0},
    .tag         = {0},
    .provides_count = crypto_provides_cnt,
    .requires_count = crypto_requires_cnt,
    .conflicts_count = 0,
    .ops = {
        .init          = crypto_plugin_init,
        .start         = crypto_plugin_start,
        .stop          = crypto_plugin_stop,
        .destroy       = crypto_plugin_destroy,
        .configure     = crypto_plugin_configure,
        .get_interface = crypto_plugin_get_interface,
    },
};

/* ============================================================
 * 导出入口：hw_plugin_entry()
 *   返回 hw_plugin_t*，由 BUS/LOADER 调用以取得插件描述符。
 * ============================================================ */
HW_CRYPTO_EXPORT
hw_plugin_t *hw_plugin_entry(void) {
    int i;

    /* 首次调用时绑定协议名表 */
    if (hw_crypto_plugin.provides == NULL) {
        static char *provides[2];
        for (i = 0; i < crypto_provides_cnt; i++)
            provides[i] = (char *)crypto_provides_arr[i];
        provides[crypto_provides_cnt] = NULL;
        hw_crypto_plugin.provides = provides;
    }
    if (hw_crypto_plugin.requires == NULL) {
        static char *requires[8];
        for (i = 0; i < crypto_requires_cnt; i++)
            requires[i] = (char *)crypto_requires_arr[i];
        requires[crypto_requires_cnt] = NULL;
        hw_crypto_plugin.requires = requires;
    }

    hw_crypto_plugin.state = HWPLUGIN_LOADED;
    return &hw_crypto_plugin;
}

#ifdef __cplusplus
}
#endif