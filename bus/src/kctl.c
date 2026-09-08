/*
 * kctl.c — BUS 内核边界客户端实现
 *
 * 封装对 /dev/hwrun (hwrun_core.ko) 的 ioctl 访问。
 * 请求号与 kernel/include/uapi/hwrun.h 对齐（generic x86_64 位布局）。
 *
 * 降级语义：设备不存在 / ioctl 失败 → 返回 HWRUN_ENOTREADY 或负 errno，
 * 绝不影响用户态 METAPROTO 主路径。
 */

#include "kctl.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

/*
 * asm-generic/ioctl.h 位布局（x86_64 与 asm-generic 一致）：
 *   dir  30..31 (NONE=0, WRITE=1, READ=2, RDWR=3)
 *   size 16..29
 *   type 8..15
 *   nr   0..7
 */
#define KCTL_NR_SHIFT   0
#define KCTL_TYPE_SHIFT 8
#define KCTL_SIZE_SHIFT 16
#define KCTL_DIR_SHIFT  30
#define KCTL_WRITE 1u
#define KCTL_READ  2u
#define KCTL_IOC(dir, type, nr, size) \
    (((uint32_t)(dir) << KCTL_DIR_SHIFT) | \
     ((uint32_t)(type) << KCTL_TYPE_SHIFT) | \
     ((uint32_t)(nr)  << KCTL_NR_SHIFT)   | \
     ((uint32_t)(size) << KCTL_SIZE_SHIFT))

/* 与 uapi/hwrun.h 相同的 magic / nr */
#define KCTL_MAGIC 0x48
#define KCTL_DESC_SIZE \
    (HWRUN_KCTL_PROTOCOL_NAME_MAX + HWRUN_KCTL_PROTOCOL_VERSION_MAX + \
     HWRUN_KCTL_PROVIDER_NAME_MAX + 8)

#define KCTL_IOC_GET_ABI             KCTL_IOC(KCTL_READ,  KCTL_MAGIC, 0x00, sizeof(uint32_t))
#define KCTL_IOC_PING                KCTL_IOC(KCTL_READ|KCTL_WRITE, KCTL_MAGIC, 0x01, sizeof(uint32_t))
#define KCTL_IOC_PROTOCOL_REGISTER   KCTL_IOC(KCTL_WRITE, KCTL_MAGIC, 0x10, KCTL_DESC_SIZE)
#define KCTL_IOC_PROTOCOL_UNREGISTER KCTL_IOC(KCTL_WRITE, KCTL_MAGIC, 0x11, KCTL_DESC_SIZE)
#define KCTL_IOC_PROTOCOL_RESOLVE    KCTL_IOC(KCTL_READ|KCTL_WRITE, KCTL_MAGIC, 0x12, KCTL_DESC_SIZE)

int hwrun_kctl_open(hwrun_kctl_t *c) {
    return hwrun_kctl_open_path(c, HWRUN_KCTL_DEVICE);
}

int hwrun_kctl_open_path(hwrun_kctl_t *c, const char *path) {
    if (!c) return HWRUN_EINVAL;
    memset(c, 0, sizeof(*c));
    c->fd = -1;
    if (!path) path = HWRUN_KCTL_DEVICE;
    snprintf(c->device, sizeof(c->device), "%s", path);

    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return HWRUN_ENOTREADY;   /* 无内核边界设备，降级 */
    c->fd = fd;
    c->connected = 1;
    return HWRUN_OK;
}

void hwrun_kctl_close(hwrun_kctl_t *c) {
    if (!c) return;
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
    c->connected = 0;
}

static int kctl_io(hwrun_kctl_t *c, unsigned long req, void *arg) {
    if (!c || c->fd < 0) return HWRUN_ENOTREADY;
    if (ioctl(c->fd, req, arg) != 0) return -errno;
    return HWRUN_OK;
}

int hwrun_kctl_get_abi(hwrun_kctl_t *c, uint32_t *abi) {
    if (!abi) return HWRUN_EINVAL;
    uint32_t v = 0;
    int rc = kctl_io(c, KCTL_IOC_GET_ABI, &v);
    if (rc != HWRUN_OK) return rc;
    *abi = v;
    return HWRUN_OK;
}

int hwrun_kctl_ping(hwrun_kctl_t *c, uint32_t *value) {
    if (!value) return HWRUN_EINVAL;
    uint32_t v = *value;
    int rc = kctl_io(c, KCTL_IOC_PING, &v);
    if (rc != HWRUN_OK) return rc;
    *value = v;   /* 内核返回 ~input */
    return HWRUN_OK;
}

int hwrun_kctl_protocol_register(hwrun_kctl_t *c, const char *protocol,
                                 const char *version, const char *provider) {
    if (!protocol || !version || !provider) return HWRUN_EINVAL;
    hwrun_kctl_desc_t d;
    memset(&d, 0, sizeof(d));
    snprintf(d.protocol, sizeof(d.protocol), "%s", protocol);
    snprintf(d.version,  sizeof(d.version),  "%s", version);
    snprintf(d.provider, sizeof(d.provider), "%s", provider);
    return kctl_io(c, KCTL_IOC_PROTOCOL_REGISTER, &d);
}

int hwrun_kctl_protocol_unregister(hwrun_kctl_t *c, const char *protocol,
                                   const char *provider) {
    if (!protocol || !provider) return HWRUN_EINVAL;
    hwrun_kctl_desc_t d;
    memset(&d, 0, sizeof(d));
    snprintf(d.protocol, sizeof(d.protocol), "%s", protocol);
    snprintf(d.provider, sizeof(d.provider), "%s", provider);
    return kctl_io(c, KCTL_IOC_PROTOCOL_UNREGISTER, &d);
}

int hwrun_kctl_protocol_resolve(hwrun_kctl_t *c, const char *protocol,
                                hwrun_kctl_desc_t *out) {
    if (!protocol || !out) return HWRUN_EINVAL;
    hwrun_kctl_desc_t d;
    memset(&d, 0, sizeof(d));
    snprintf(d.protocol, sizeof(d.protocol), "%s", protocol);
    int rc = kctl_io(c, KCTL_IOC_PROTOCOL_RESOLVE, &d);
    if (rc != HWRUN_OK) return rc;
    *out = d;
    return HWRUN_OK;
}