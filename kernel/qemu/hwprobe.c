// HWRun ioctl 运行级验证探针（QEMU minimal initramfs 内运行）。
// 编译：gcc -static -O2 -I<kernel>/include/uapi -o hwprobe hwprobe.c
// 依赖 <linux/ioctl.h>/<linux/types.h>（宿主 linux-libc-dev 即可，ABI 与 arch 无关）。
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "hwrun.h"

static int failures = 0;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) {                                                   \
            printf("PASS %s\n", msg);                                 \
        } else {                                                      \
            printf("FAIL %s (errno=%d %s)\n", msg, errno,             \
                   strerror(errno));                                  \
            failures++;                                               \
        }                                                             \
    } while (0)

int main(void)
{
    int fd = open("/dev/hwrun", O_RDWR);
    if (fd < 0) {
        printf("FAIL open /dev/hwrun (errno=%d %s)\n", errno, strerror(errno));
        return 1;
    }

    /* GET_ABI */
    unsigned int abi = 0;
    errno = 0;
    int rc = ioctl(fd, HWRUN_IOC_GET_ABI, &abi);
    CHECK(rc == 0 && abi == HWRUN_ABI_VERSION, "ioctl GET_ABI (abi=1)");
    printf("  abi=%u\n", abi);

    /* PING：取反回环 */
    unsigned int ping = 0x11223344u;
    errno = 0;
    rc = ioctl(fd, HWRUN_IOC_PING, &ping);
    CHECK(rc == 0 && ping == ~0x11223344u, "ioctl PING (bitwise not)");

    /* 非法命令 → ENOTTY */
    errno = 0;
    rc = ioctl(fd, _IOW(HWRUN_IOC_MAGIC, 0x7f, unsigned int), &ping);
    CHECK(rc < 0 && errno == ENOTTY, "ioctl unknown cmd -> ENOTTY");

    /* REGISTER */
    struct hwrun_protocol_desc d;
    memset(&d, 0, sizeof(d));
    strcpy(d.protocol, "bus.test");
    strcpy(d.version, "1.0.0");
    strcpy(d.provider, "probe");
    d.implementation = (__u64)0x1234;
    errno = 0;
    rc = ioctl(fd, HWRUN_IOC_PROTOCOL_REGISTER, &d);
    CHECK(rc == 0, "ioctl REGISTER bus.test");

    /* 重复 REGISTER → EEXIST */
    errno = 0;
    rc = ioctl(fd, HWRUN_IOC_PROTOCOL_REGISTER, &d);
    CHECK(rc < 0 && errno == EEXIST, "ioctl REGISTER dup -> EEXIST");

    /* 空 protocol → EINVAL */
    struct hwrun_protocol_desc empty;
    memset(&empty, 0, sizeof(empty));
    errno = 0;
    rc = ioctl(fd, HWRUN_IOC_PROTOCOL_REGISTER, &empty);
    CHECK(rc < 0 && errno == EINVAL, "ioctl REGISTER empty -> EINVAL");

    /* RESOLVE：取回注册的 version/provider（implementation 内核置 NULL） */
    memset(&d, 0, sizeof(d));
    strcpy(d.protocol, "bus.test");
    errno = 0;
    rc = ioctl(fd, HWRUN_IOC_PROTOCOL_RESOLVE, &d);
    CHECK(rc == 0 && strcmp(d.version, "1.0.0") == 0 &&
              strcmp(d.provider, "probe") == 0 && d.implementation == 0,
          "ioctl RESOLVE bus.test roundtrip");
    printf("  resolved version=%s provider=%s impl=%llu\n", d.version, d.provider,
           (unsigned long long)d.implementation);

    /* RESOLVE 未注册协议 → ENOENT */
    memset(&d, 0, sizeof(d));
    strcpy(d.protocol, "no.such");
    errno = 0;
    rc = ioctl(fd, HWRUN_IOC_PROTOCOL_RESOLVE, &d);
    CHECK(rc < 0 && errno == ENOENT, "ioctl RESOLVE missing -> ENOENT");

    /* UNREGISTER */
    memset(&d, 0, sizeof(d));
    strcpy(d.protocol, "bus.test");
    strcpy(d.provider, "probe");
    errno = 0;
    rc = ioctl(fd, HWRUN_IOC_PROTOCOL_UNREGISTER, &d);
    CHECK(rc == 0, "ioctl UNREGISTER bus.test");

    /* UNREGISTER 后 RESOLVE → ENOENT（释放生效） */
    memset(&d, 0, sizeof(d));
    strcpy(d.protocol, "bus.test");
    errno = 0;
    rc = ioctl(fd, HWRUN_IOC_PROTOCOL_RESOLVE, &d);
    CHECK(rc < 0 && errno == ENOENT, "ioctl RESOLVE after unregister -> ENOENT");

    close(fd);
    if (failures == 0) {
        printf("HWRUN_PROBE_PASS\n");
        return 0;
    }
    printf("HWRUN_PROBE_FAIL (%d)\n", failures);
    return 1;
}
