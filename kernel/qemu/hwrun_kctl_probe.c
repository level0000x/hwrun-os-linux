// HWRun kctl 内核边界客户端运行级探针（QEMU initramfs 内运行）。
// 与 hwprobe.c 的裸 ioctl 不同：此处走 BUS 侧客户端库 bus/src/kctl.c，
// 验证用户态封装(手工 ioctl 位布局 + 负 errno 语义)与内核 /dev/hwrun 的真交互。
// 编译：gcc -static -O2 -I<root>/include -I<root>/bus/src \
//       -o hwrun_kctl_probe hwrun_kctl_probe.c <root>/bus/src/kctl.c
#include <stdio.h>
#include <string.h>

#include "kctl.h"

static int failures = 0;

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) {                                                   \
            printf("PASS %s\n", msg);                                 \
        } else {                                                      \
            printf("FAIL %s (rc=%d)\n", msg, rc);                     \
            failures++;                                               \
        }                                                             \
    } while (0)

int main(void)
{
    hwrun_kctl_t c;
    int rc = hwrun_kctl_open(&c);
    CHECK(rc == HWRUN_OK, "kctl open /dev/hwrun");

    uint32_t abi = 0;
    rc = hwrun_kctl_get_abi(&c, &abi);
    CHECK(rc == HWRUN_OK && abi == HWRUN_KCTL_ABI_VERSION, "kctl GET_ABI (abi=1)");
    printf("  abi=%u\n", abi);

    uint32_t ping = 0x11223344u;
    rc = hwrun_kctl_ping(&c, &ping);
    CHECK(rc == HWRUN_OK && ping == ~0x11223344u, "kctl PING (bitwise not)");

    rc = hwrun_kctl_protocol_register(&c, "bus.gtest", "2.0.0", "kctlprobe");
    CHECK(rc == HWRUN_OK, "kctl REGISTER bus.gtest");

    rc = hwrun_kctl_protocol_register(&c, "bus.gtest", "2.0.0", "kctlprobe");
    CHECK(rc == -EEXIST, "kctl REGISTER dup -> -EEXIST");

    rc = hwrun_kctl_protocol_register(&c, "", "2.0.0", "kctlprobe");
    CHECK(rc == -EINVAL, "kctl REGISTER empty proto -> -EINVAL");

    hwrun_kctl_desc_t out;
    memset(&out, 0, sizeof(out));
    rc = hwrun_kctl_protocol_resolve(&c, "bus.gtest", &out);
    CHECK(rc == HWRUN_OK && strcmp(out.version, "2.0.0") == 0 &&
              strcmp(out.provider, "kctlprobe") == 0 && out.implementation == 0,
          "kctl RESOLVE roundtrip");
    printf("  resolved version=%s provider=%s impl=%llu\n", out.version, out.provider,
           (unsigned long long)out.implementation);

    rc = hwrun_kctl_protocol_resolve(&c, "no.such", &out);
    CHECK(rc == -ENOENT, "kctl RESOLVE missing -> -ENOENT");

    rc = hwrun_kctl_protocol_unregister(&c, "bus.gtest", "kctlprobe");
    CHECK(rc == HWRUN_OK, "kctl UNREGISTER bus.gtest");

    rc = hwrun_kctl_protocol_resolve(&c, "bus.gtest", &out);
    CHECK(rc == -ENOENT, "kctl RESOLVE after unregister -> -ENOENT");

    /* 降级语义：设备缺失 → ENOTREADY，不崩溃 */
    hwrun_kctl_t d;
    rc = hwrun_kctl_open_path(&d, "/dev/no-such-hwrun");
    CHECK(rc == HWRUN_ENOTREADY, "kctl open missing device -> ENOTREADY");

    hwrun_kctl_close(&c);
    if (failures == 0) {
        printf("HWRUN_KCTL_PASS\n");
        return 0;
    }
    printf("HWRUN_KCTL_FAIL (%d)\n", failures);
    return 1;
}
