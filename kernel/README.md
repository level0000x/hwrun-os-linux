# HWRun Linux 内核层

`kernel/` 目录收敛为面向 Linux kbuild 的集成层。项目使用 Linux 作为内核实现，
不再维护自研 i386 微内核；Linux 源码位于外部 `linux-src/`（不入库），本目录只
持有 HWRun 的配置、内核模块与所需头文件。

## 目录结构

- `config/`：Linux 内核 profile——`minimal.config`（机制最小化）与 `host.config`
  （当前 POSIX 用户态插件链所需机制）
- `Makefile`：面向外部 `LINUX_SRC` 的 kbuild 编排（config / kernel / modules）
- `modules/`：HWRun 内核边界模块，`hwrun_core.ko` 提供 `/dev/hwrun` 与协议
  注册/注销/解析 ioctl
- `include/`：模块构建所需头——`hwrun_kernel.h`（内核侧模块 API）、
  `uapi/hwrun.h`（用户态 ioctl ABI，`bus/src/kctl.c` 对齐用）

自研 i386 微内核残留（`src/`、`boot/boot.S`、`linker.ld` 及配套内核头）已废弃并
从仓库移除，不再参与任何构建。

## 构建

```sh
make -C kernel LINUX_SRC=/path/to/linux oldconfig
make -C kernel PROFILE=minimal LINUX_SRC=/path/to/linux -j4 kernel
make -C kernel PROFILE=host LINUX_SRC=/path/to/linux modules
```

`minimal` profile 从 Linux `allnoconfig` 起步合并 `config/minimal.config`，只保留
不可插件化的机制（启动、地址空间、调度、系统调用、ELF、initramfs、模块装载、
基础事件原语）；`host` profile 追加 VFS、ext4/overlayfs、`/proc`、`/sys`、
socket、namespace、cgroup、seccomp 等现有插件所需的机制。

构建产物输出到 `kernel/build/`（不入库），模块产物留在 `modules/`（均被
`.gitignore` 覆盖，不入库）。

## 边界

- Linux 提供进程、虚拟内存、调度、驱动、文件系统、ELF 执行、namespace 等机制。
- `bus/` 用户态组件总线仍按 `hw_plugin_entry()` ABI 加载 `.so` 插件。
- 需要内核权限的边界能力以薄 `.ko` 放于 `modules/`，通过 UAPI 或协议注册接入。

## 依赖

- 与目标构建环境匹配的 Linux 内核源码（`LINUX_SRC`）
- GCC、GNU Make 及所选 Linux 版本要求的内核构建依赖
