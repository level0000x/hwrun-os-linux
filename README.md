# HWRun OS

以插件、协议、参数和 Git 为核心的操作系统组件容器：

```
HWRun OS = Linux 机制层(kbuild profiles) + BUS + METAPROTO + PARAM + LOG + GIT + 用户态插件(.so)
```

- Linux 内核只提供不可插件化的机制（`kernel/config/` 提供 minimal / host 两种 profile）。
- BUS 负责插件发现、依赖解析、生命周期、协议注册与运行时注入。
- 插件之间经 METAPROTO 注册的协议通信；PARAM 决定行为；GIT 记录状态与历史。
- 当前已验证插件链：git, hap, pmp, fsp, np, sp, crypto, loader。

详细架构与实现基线见 [docs/design.md](docs/design.md)，编码规范见 [docs/coding-style.md](docs/coding-style.md)。

## 构建

以 **CMake 为统一构建系统**（`Makefile` 保留做兼容，新代码请用 CMake）。

依赖（Ubuntu/WSL）：

```sh
sudo apt install cmake ninja-build pkg-config libssl-dev libcmocka-dev \
                 clang-format clang-tidy valgrind
```

构建与测试：

```sh
cmake -S . -B build                 # 配置（默认 Release）
cmake --build build -j              # 构建 hwrun-core/hwrun-bus + 8 个插件 .so
ctest --test-dir build --output-on-failure   # 跑 CMocka + 真协议测试
```

常用开关：

```sh
-DHWRUN_BUILD_TESTS=OFF             # 关测试
-DHWRUN_ENABLE_SANITIZER=address    # address|undefined|thread|all
-DHWRUN_WERROR=ON                   # -Werror 硬门禁
```

兼容入口（旧 Makefile）：

```sh
make            # = bus + plugins
make test       # 顶层测试
```

## 运行

启动整条插件链（无子命令时进入 PID1 常驻，支持 SIGTERM/SIGINT 优雅停机）：

```sh
HWRUN_PLUGINS=/path/to/repo ./build/bus/hwrun-bus
# HWRun OS booted (8 plugins, 13 protocols)
```

CLI 子命令（bus 源码在 `bus/src/`）：

```sh
hwrun-bus status                 # 系统状态
hwrun-bus plugins list           # 插件列表
hwrun-bus protocols list         # 协议列表
hwrun-bus params list|get|set    # 参数操作
hwrun-bus kctl abi|ping|resolve  # 内核边界客户端（无 /dev/hwrun 时降级）
```

## 内核

`kernel/` 收敛为 Linux kbuild 集成（自研 i386 微内核已废弃）：

```sh
make -C kernel PROFILE=minimal LINUX_SRC=/path/to/linux-src kernel   # 内核
make -C kernel PROFILE=minimal LINUX_SRC=/path/to/linux-src modules  # hwrun_core.ko
```

`linux-src/` 为外部 Linux 源码树，不入库。

## 目录结构

```text
include/         公共契约头（hwrun.h + 插件 SDK hwrun_plugin.h）
bus/             BUS 核心（hwrun-core 静态库 + hwrun-bus 可执行）
git hap pmp fsp np sp crypto loader/   8 个用户态协议插件
tests/           CMocka 单测 + 真协议集成测试（ctest 聚合）
kernel/          Linux kbuild profiles + hwrun_core.ko 模块 + uapi
```

## 许可

本项目以 **GNU GPL v3.0** 发布，全文见 [LICENSE](LICENSE)。

- 本项目自身的代码：GPLv3。
- 其中引用的 **Linux 内核**（`linux-src/` 外部源码树，不入库）为 **GPLv2**，按其自身条款分发；
  本仓库不打包内核源码。
- 第三方组件（若引入）各自许可见其自身 LICENSE，并在其目录内声明。

