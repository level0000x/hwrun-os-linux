# HWRun Linux Kernel Layer

HWRun now uses Linux as the kernel implementation. The project does not copy or vendor the Linux source tree. `LINUX_SRC` points to an external Linux checkout, and this directory owns the HWRun configuration plus kernel modules.

## Build

```sh
make -C kernel LINUX_SRC=/path/to/linux oldconfig
make -C kernel LINUX_SRC=/path/to/linux -j4 kernel
make -C kernel LINUX_SRC=/path/to/linux modules
```

The default configuration starts from Linux `allnoconfig` and merges
`hwrun.config`, so disabled subsystems stay out unless they are explicitly
added to the fragment. To regenerate the pruned configuration only:

```sh
make -C kernel LINUX_SRC=/path/to/linux prune
```

The first module is `modules/hwrun_core.ko`. It exposes `/dev/hwrun` and the UAPI in `include/uapi/hwrun.h`.

The pruned x86_64 build produces `build/linux/arch/x86/boot/bzImage`.

## Architecture

- Linux supplies process management, virtual memory, scheduling, IPC primitives, drivers, filesystems, ELF execution, namespaces and security mechanisms.
- `bus/` remains the user-space component bus and continues to load `.so` plugins through the existing `hw_plugin_entry()` ABI.
- HAP, PMP, LOADER and later hardware-facing components should be implemented as Linux kernel modules under `kernel/modules/` and registered through explicit UAPI or protocol adapters.
- The previous freestanding i386 prototype sources remain in `boot/` and `src/` for reference only; they are not part of the default build anymore.

## Requirements

- Linux kernel source matching the target build environment
- GCC and GNU Make
- Kernel build dependencies required by the selected Linux version
- Root privileges only when installing modules or booting the resulting kernel
