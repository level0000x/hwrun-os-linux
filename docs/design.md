 # HWRun OS 设计与实现基线

版本：0.3
状态：当前实现基线（底层工程化重构完成）
更新时间：2026-09-08

> 本文档为架构与实现基线。编码规范、构建方式见 docs/coding-style.md。
> 0.3 变更摘要：CMake 统一构建、全库负 errno 错误码、bus 线程锁与内存
> 所有权修复、插件入口统一 SDK 宏、CMocka 测试体系、CI 门禁、废弃自研
> i386 微内核（kernel 收敛到 Linux kbuild）。

## 1. 项目定位

HWRun OS 是一个以插件、协议、参数和 Git 为核心的操作系统组件容器：

```text
HWRun OS = Linux 机制层 + BUS + METAPROTO + PARAM + LOG + GIT + 插件
```

核心原则：

- Linux 内核只提供不可插件化的机制。
- BUS 负责插件发现、加载、依赖、生命周期和卸载。
- 插件之间通过 METAPROTO 注册的协议通信。
- PARAM 决定系统和插件的行为。
- GIT 记录系统状态、配置、插件和历史变化。
- 文件系统、网络、安全、硬件、格式加载器、UI 和分布式能力都通过插件提供。

## 2. 内核边界

### 2.1 必须保留在 Linux 机制层

- CPU 启动和架构初始化
- 虚拟内存和地址空间
- 进程/线程基础
- 系统调用入口
- 中断和时钟
- 基础调度机制
- ELF 基础执行机制
- 内核模块装载机制
- 基础设备、内存和同步原语

这些是 Linux 运行用户态插件所需的机制，不属于可由普通 `.so` 插件替换的策略。

### 2.2 不应写死在内核中的策略

- 文件系统选择和挂载策略
- 网络协议和路由策略
- 进程管理策略
- 调度策略
- 权限和安全策略
- 存储后端
- Git 操作
- 参数行为
- 集群策略
- 用户界面
- 应用和工具

需要内核权限的能力可以使用薄 Linux `.ko` 作为边界桥接，但协议、策略、生命周期和参数逻辑应尽量留在用户态插件。

## 3. 两种 Linux 内核 profile

项目使用 Linux kbuild，不再维护自制 i386 微内核作为默认实现。Linux 源码位于本地工作区的 `linux-src/`，该目录不提交到 Git 仓库。

### 3.1 minimal profile

`kernel/config/minimal.config` 只保留机制内核需要的能力：

- x86_64 启动
- 地址空间、调度和系统调用基础
- ELF
- initramfs
- 内核模块
- futex、epoll、eventfd、signalfd、timerfd
- devtmpfs 和 tmpfs

该 profile 不主动提供 ext4、overlayfs、TCP/IP、namespace、cgroup、`/proc`、`/sys`、桌面、声音、图形和具体硬件策略。

构建：

```sh
make -C kernel PROFILE=minimal LINUX_SRC=/root/linux-src JOBS=2 kernel
```

### 3.2 host profile

`kernel/config/host.config` 用于运行当前已经实现的 POSIX 用户态插件链，保留：

- VFS、ext4、overlayfs
- ELF 和脚本执行
- `/proc`、`/sys`
- UNIX socket、IPv4、网络设备
- namespace、cgroup、user namespace、seccomp
- 内核模块

构建：

```sh
make -C kernel PROFILE=host LINUX_SRC=/root/linux-src JOBS=2 kernel
```

## 4. 启动和插件链

设计启动顺序：

```text
BIOS/UEFI
	-> Linux 机制层
	-> METAPROTO
	-> BUS
	-> PARAM
	-> LOG
	-> GIT
	-> HAP
	-> PMP
	-> FSP
	-> NP
	-> SP
	-> CRYPTO
	-> LOADER
	-> 其他插件
```

当前用户态插件的实际加载流程：

```text
plugin.yml
	-> BUS 扫描
	-> dlopen(.so)
	-> dlsym(hw_plugin_entry)
	-> init()
	-> METAPROTO 注册 provides
	-> start()
```

当前已验证的插件：

```text
git, hap, pmp, fsp, np, sp, crypto, loader
```

在同一个 BUS 进程中已验证全部启动成功：

```text
HWRun OS booted (8 plugins, 13 protocols)
```

## 5. 模块分层

### 5.1 基础协议

- `METAPROTO`：协议注册、解析、版本和订阅
- `BUS`：插件发现、依赖、生命周期、加载和卸载
- `PARAM`：参数树、配置持久化和动态更新
- `LOG`：统一日志入口
- `GIT`：状态、提交、版本、同步、回滚和审计

### 5.2 系统能力协议

- `HAP`：CPU、内存、磁盘、网络和硬件信息
- `PMP`：进程、线程、信号、调度和任务
- `FSP`：文件、目录、权限、路径和挂载
- `NP`：socket、TCP/UDP、接口和解析
- `SP`：认证、密钥、哈希、加密和签名
- `CRYPTO`：密码学原语，作为 SP 的扩展能力
- `LOADER`：格式检测、加载、运行和卸载

### 5.3 扩展和应用协议

- `DRIVER`、`INPUT`、`DISPLAY`、`AUDIO`、`POWER`
- `STORAGE`、`PERMISSION`、`AUDIT`、`HOTPLUG`、`FIRMWARE`
- `CLUSTER`、`CONSENSUS`、`NODE_DISCOVERY`、`FS_TRANSFER`
- `INSTANCE`、`COMPRESS`、`TERMINAL`
- `SANDBOX`、`UI`、`SHELL`、`CONSOLE`、`DESKTOP`、`PKGMGR`、`MONITOR`

这些模块应优先作为用户态协议插件实现；只有需要直接访问内核钩子、设备或安全边界的部分才增加薄 `.ko`。

## 6. 当前代码状态

已经完成：

- Linux v6.1 源码工作区准备
- Linux kbuild 适配层
- minimal/host 内核 profile（均在 WSL Ubuntu 2 环境以多核固化构建，产物验证通过）
- `bzImage` 构建（minimal 1.6MB / host 2.8MB）
- `hwrun_core.ko` 构建（vermagic 6.1.0）
- `hwrun_core` 基础协议注册表和 ioctl UAPI
- 顶层插件构建编排修复
- 8 个现有用户态插件通过 BUS 启动链验证
- BUS 内核边界客户端 `kctl`（bus/src/kctl.{c,h}），封装 `/dev/hwrun` ioctl，无设备时降级返回
- 真协议调用自测 `tests/`：dlopen 插件 → metaproto 解析 → 真实调用 HAP/PMP/FSP/NP/LOADER ops，38 项断言全部通过
- CRYPTO 插件 Makefile 支持双平台（MSYS2/mingw64 与 Linux/系统 OpenSSL），Linux 下产出 ELF `.so`

### 6.1 底层工程化重构（0.3）

- 构建系统迁移 CMake：顶层 + bus(hwrun-core 静态库/hwrun-bus) + 8 插件 + tests，统一告警与 ctest；顶层 Makefile 保留兼容
- 错误码全库负 errno 化：`HWRUN_*` 收敛为标准 errno 别名 + 私有域 `HW_EBASE`（见 include/hwrun.h）；修复 `loader_run` 返正 bug、`main` 魔法数
- bus 并发安全：`hwlock` 读写锁抽象 + RAII 宏，接入 metaproto/param/log/plugins 链表；通知"锁内快照→出锁派发"
- bus 内存所有权：修复 scan 元数据泄漏，`hw_plugin_free_meta` + shutdown 全释放（valgrind 零泄漏）
- PID1 信号处理：SIGTERM/SIGINT 优雅停机、SIGPIPE 忽略
- 插件 SDK：`include/hwrun_plugin.h` 的 `HWRUN_PLUGIN_BIND/DEFINE` 统一 8 插件入口，消灭多套手写骨架；运行时 fprintf 转 HWAPI
- 测试体系：CMocka + ctest 聚合（test_errno/param/metaproto/yml/lock + test_proto 真协议）
- CI 门禁：`.github/workflows/ci.yml`（build/sanitize/static/make 四 job）；`.clang-format`/`.clang-tidy`/`.editorconfig`
- kernel 收敛：废弃自研 i386 微内核（src/boot/linker.ld 及配套头移除），保留 Linux kbuild 集成（config profiles + hwrun_core.ko + uapi）

当前 `hwrun_core.ko` 提供：

- `/dev/hwrun`
- ABI 版本查询
- ping
- 协议注册
- 协议注销
- 协议解析

它目前是内核边界桥接模块，不是现有 BUS 的强依赖。现有 BUS 继续使用进程内 METAPROTO 路由，这是当前可运行的主路径。`kctl` 客户端是面向该桥接的可选调用路径，且与用户态 METAPROTO 解耦。

### 6.2 占位清理与启动校验（0.4 B 组）

- `hw_metaproto_export_api`（空壳）与 `hw_metaproto_api_t` 结构、`HWRUN_USE_TABLE_API` 条件段已删除：全仓无任何调用方，按 YAGNI 判定为死代码；原功能无需该 API，直接调用裸 `hw_metaproto_register/unregister/resolve/list/subscribe/notify/check_deps` 函数。
- `hw_param_mark_revision` 落地为事件钩子：param 属依赖链第 1 环，不直接依赖 GIT 头；改为在 ctx 上提供可选回调 `on_revision(userdata, reason)`，由 bus（第 2 环）在 `hw_bus_init` 装配。回调 resolve GIT 协议，在 git 插件已启动且 auto_commit 开启时把参数状态文件 `add + commit("param: <reason>")`；git 就绪前静默跳过，commit 期间设重入守卫防递归。
- `boot_chain` 启动校验补齐：`hw_plugin_start` 在 requires 检查之后新增 conflicts 检查（声明冲突的协议已被注册 → 拒绝启动、置 ERROR、返回 `HWRUN_ECONFLICT`）；`boot_chain` 末尾对 boot_order 表外的已发现插件给出"未纳入启动序，不会自动启动"告警。

### 6.3 门禁收尾（0.4 E 组：格式清零 / 插件单测 / 覆盖率汇总）

- **E1 格式存量清零**：8 插件目录（src/include/selftest/test）+ bus/tests 全量 `clang-format -i`（纯格式，无语义改动）；本地 dry-run 0 违规。CI static job 移除 `continue-on-error`：以 `git ls-files` 列出非 `.trae` 文件跑 `clang-format --dry-run --Werror`，排除 `kernel/`。
- **E2 插件层单测**：hap/pmp/git/crypto 各新增 CMocka dlopen 集成单测（tests/test_{hap,pmp,git,crypto}.c），加载插件 `.so` 后经 METAPROTO 真调协议 ops；git 测试在隔离仓库覆盖 add/commit/log/status/compact，crypto 覆盖 hash/HMAC(RFC4231)/AES/RSA。tests/CMakeLists 的 `_dlopen_tests` 组与 test_proto 同构（ENABLE_EXPORTS + add_dependencies + HWRUN_PLUGIN_ROOT）。
- **E3 lcov 覆盖率汇总**：`HWRUN_ENABLE_COVERAGE=ON` 下，插件 `.so` 与全部测试目标统一 `--coverage` 插桩（原来只插桩 hwrun-core，插件与 dlopen 测试漏插导致覆盖不全）。单命令自助目标：
  ```sh
  cmake -S . -B build-cov -DHWRUN_ENABLE_COVERAGE=ON
  cmake --build build-cov --target coverage-report -j4   # 清计数→ctest→lcov→genhtml
  # 报告：build-cov/coverage/index.html
  ```
  基线（WSL，2026-09）：行覆盖 46.2%（1500/3245）、函数 49.8%（164/329），覆盖范围 bus/hwrun-core + 8 插件源码（测试桩代码已排除）。
- **kernel 模块真编译**：本地 `linux-src/`（Linux 6.1.0）执行 `make -C kernel PROFILE=minimal JOBS=4 modules`，`hwrun_core.o` 编译通过——uapi desc 的 6 条 ABI `_Static_assert`（152B/各字段偏移）在真实 kbuild 下生效，产出 `hwrun_core.ko`（编译级验证；运行级 ioctl 验证仍需装入真实内核）。

## 7. 已知差异

1. TXT 文档同时描述了自研微内核和 Linux 内核两条路线，当前实现选择 Linux。
2. 文档中许多模块写成 `.ko`，当前 HAP/PMP/FSP/NP/SP/LOADER 主要是用户态 `.so`。
3. 设计中的微内核 IPC 尚未成为当前 BUS 的通信 ABI；当前插件使用 POSIX 运行时。
4. 完整 ISO、rootfs、APT 仓库和 bootloader 流程尚未完成。
5. 参数注入和 Git 状态管理尚未覆盖所有插件。
6. 文档中的协议版本兼容规则还没有完全统一到所有插件。
7. 内核协议路由和用户态 METAPROTO 目前是两套路由表，尚未做统一桥接。
8. 构建产物（bzImage/vmlinux/`.ko`/`.so`）仅存在于本地工作区，未入库；`hwrun_core.ko` 尚未装入真实 Linux 内核做 ioctl 运行级验证（需引导 6.1 内核）。
9. ~~plugin.yml 仍有嵌套 map 与扁平纯字符串两种形态并存，单一样式收敛（统一嵌套 `plugin:` 根 + protocol/version map）~~（已收敛：8 个 yml 统一格式，解析器缩进感知修复子键覆盖问题）。

## 8. 后续实施顺序

1. ~~固化 `minimal` 和 `host` profile 的构建、产物和启动测试~~（已在 WSL 完成构建固化）
2. ~~为 BUS 增加稳定的内核边界客户端，按需访问 `/dev/hwrun`~~（`kctl` 已实现）
3. ~~统一 `plugin.yml`、`hw_plugin_entry()`、协议版本和错误码~~（0.3：SDK 宏统一入口 + 负 errno 收敛；plugin.yml 单一样式已收敛）
4. ~~将 PARAM、LOG、GIT 接口注入现有插件生命周期~~（已注入，见 6.1）
5. ~~为 HAP/PMP/FSP/NP/LOADER 增加真正的协议调用测试，而不是只测试启动~~（CMocka 体系，6 组用例通过）
6. 对确实需要内核权限的能力增加薄 `.ko`，避免重复实现用户态插件逻辑。
7. 将 `hwrun_core.ko` 装入引导的 minimal 内核，完成 `kctl` 客户端在内核边界的运行级验证。
8. 最后再推进 bootloader、rootfs、ISO 和完整发行版构建。
9. ~~底层工程化重构（构建/错误码/并发/内存/信号/测试/门禁）~~（0.3 已完成并验证）

## 9. 基线提交

架构与工程化基线已推送到 `origin/main`：

```text
87ec2fa build: 迁移统一 CMake 构建树并新增 hwlock 线程锁抽象
0afb837 refactor: 全库错误码收敛为负 errno 语义
52843dc refactor: bus 锁接入——子系统头加 hwlock 字段,补 runtime.c 入库
17549e8 feat: 新增插件 SDK 头并引入 CMocka 单测体系
637f8ff chore: 增加 clang-format/tidy 与 editorconfig 门禁配置及 CI
7260785 refactor: 废弃自研 i386 微内核,收敛 kernel 到 Linux kbuild 集成
bef330b 建立机制最小化内核 profile
```
