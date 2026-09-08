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
#include "hwrun_plugin.h"
#include "crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 由实现文件提供（crypto_impl.c） */
extern hw_crypto_ops_t hw_crypto_ops;

/* ---- 生命周期 ----
 * CRYPTO 子模块为无状态加密原语宿主，调用时按需分配 EVP 上下文，
 * 故 init/start/stop/destroy 无需持有长期资源，均返回 HWRUN_OK。 */
static int g_plugin_state = HWPLUGIN_LOADED;

static int crypto_plugin_init(hw_plugin_t *self) {
    (void)self;
    g_plugin_state = HWPLUGIN_LOADED;
    HWAPI_LOGI("crypto", "init: provider=%s",
               HWAPI_PARAM_GET("crypto.provider") ? HWAPI_PARAM_GET("crypto.provider") : "openssl");
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
static int crypto_plugin_configure(hw_plugin_t *self, const char *key, const char *value) {
    (void)self;
    (void)key;
    (void)value;
    return HWRUN_OK;
}

/* get_interface：对外提供 CRYPTO 协议接口指针 */
static void *crypto_plugin_get_interface(const char *protocol) {
    if (!protocol) return NULL;
    if (strcmp(protocol, HWPROTO_CRYPTO) == 0) return &hw_crypto_ops;
    return NULL;
}

/* 生命周期 ops 表：由 SDK 宏装配进描述符 */
static hw_plugin_ops_t g_ops = {
    .init = crypto_plugin_init,
    .start = crypto_plugin_start,
    .stop = crypto_plugin_stop,
    .destroy = crypto_plugin_destroy,
    .configure = crypto_plugin_configure,
    .get_interface = crypto_plugin_get_interface,
};

/* 协议清单：以 NULL 哨兵结尾的只读数组（.so 静态数据，与 plugin.yml 一致） */
static const char *const g_provides[] = {"CRYPTO", NULL};
static const char *const g_requires[] = {"SP", "PARAM", "LOG", "METAPROTO", NULL};

HWRUN_PLUGIN_BIND()
HWRUN_PLUGIN_DEFINE(
    "crypto", "Cryptographic Protocol (SP submodule)", "1.0.0", HWPLUGIN_TYPE_CRYPTO,
    "HWRun OS CRYPTO protocol: symmetric/asymmetric encryption, hash, HMAC, sign, key mgmt, RNG",
    &g_ops, g_provides, g_requires)

#ifdef __cplusplus
}
#endif