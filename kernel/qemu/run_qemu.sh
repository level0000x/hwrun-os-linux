#!/bin/bash
# HWRun 内核模块运行级验证
# 前置：make -C kernel PROFILE=qemu JOBS=4 kernel modules
#        （或 make -C kernel PROFILE=qemu kernel 后再 modules）
# 流程：编译 hwprobe(static) -> 组装 gzip initramfs(busybox-static + .ko + probe)
#       -> qemu -nographic 引导 bzImage -> 捕获 HWPROBE_PASS/FAIL
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
KOUT="$ROOT/kernel/build/linux"
BZIMAGE="$KOUT/arch/x86/boot/bzImage"
KOMOD="$ROOT/kernel/modules/hwrun_core.ko"
UAPI="$ROOT/kernel/include/uapi"

STAGE="$(mktemp -d /tmp/hwrun-qemu.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT

[ -f "$BZIMAGE" ] || { echo "missing $BZIMAGE (先 make -C kernel PROFILE=qemu kernel)"; exit 1; }
[ -f "$KOMOD" ]  || { echo "missing $KOMOD (先 make -C kernel modules)"; exit 1; }

echo "==> compile static hwprobe"
gcc -static -O2 -I"$UAPI" -o "$STAGE/hwprobe" "$ROOT/kernel/qemu/hwprobe.c"
file "$STAGE/hwprobe"

echo "==> compile static kctl client probe (bus/src/kctl.c)"
gcc -static -O2 -I"$ROOT/include" -I"$ROOT/bus/src" -o "$STAGE/hwrun_kctl_probe" \
    "$ROOT/kernel/qemu/hwrun_kctl_probe.c" "$ROOT/bus/src/kctl.c"
file "$STAGE/hwrun_kctl_probe"

echo "==> assemble initramfs (busybox-static + hwrun_core.ko + probes)"
mkdir -p "$STAGE/root"/{bin,dev,proc,sys,etc}
cp /bin/busybox "$STAGE/root/bin/busybox"          # busybox-static：静态链接
( cd "$STAGE/root/bin" && ./busybox --install -s . )   # applet 软链(sh/mount/insmod/reboot/...)
cp "$KOMOD" "$STAGE/root/hwrun_core.ko"
cp "$STAGE/hwprobe" "$STAGE/root/hwprobe"
cp "$STAGE/hwrun_kctl_probe" "$STAGE/root/hwrun_kctl_probe"

cat > "$STAGE/root/init" <<'EOF'
#!/bin/busybox sh
export PATH=/bin
echo '=== init: mount proc/sys/devtmpfs ==='
busybox mount -t proc proc /proc
busybox mount -t sysfs sysfs /sys
busybox mount -t devtmpfs devtmpfs /dev
echo '=== init: insmod hwrun_core.ko ==='
busybox insmod /hwrun_core.ko || { echo HWRUN_INSMOD_FAIL; busybox reboot -f; }
ls -l /dev/hwrun
echo '=== init: run ioctl probe ==='
/hwprobe
rc=$?
echo "probe rc=$rc"
echo '=== init: run kctl client probe ==='
/hwrun_kctl_probe
rc2=$?
echo "kctl probe rc=$rc2"
[ $rc -eq 0 ] && [ $rc2 -eq 0 ] && echo HWRUN_RUNTIME_PASS || echo HWRUN_RUNTIME_FAIL
busybox reboot -f
EOF
chmod +x "$STAGE/root/init"

( cd "$STAGE/root" && find . -print | cpio -o -H newc 2>/dev/null | gzip -9 > "$STAGE/initramfs.gz" )

echo "==> qemu boot (serial console, -no-reboot auto-exit)"
LOG="$STAGE/boot.log"
timeout 90 qemu-system-x86_64 -m 256 -kernel "$BZIMAGE" \
    -initrd "$STAGE/initramfs.gz" -nographic -no-reboot \
    -append "console=ttyS0 rdinit=/init panic=-1" > "$LOG" 2>&1 || true

echo "==> tail of boot log"
cp "$LOG" /tmp/hwrun-qemu-boot.log    # 调试保留：/tmp/hwrun-qemu-boot.log
grep -aE 'hwrun|HWRUN|===|probe|PASS|FAIL|/dev/hwrun|insmod|mount|Kernel panic' "$LOG" | tail -30 || tail -25 "$LOG"

if grep -q HWRUN_RUNTIME_PASS "$LOG"; then
    echo "RESULT: PASS (hwrun_core.ko runtime ioctl verified in qemu)"
    exit 0
fi
echo "RESULT: FAIL (see log)"
exit 1
