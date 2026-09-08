 # HWRun OS 设计与实现基线

版本：0.1
状态：当前实现基线
更新时间：2026-09-08

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
- minimal/host 内核 profile
- `bzImage` 构建
- `hwrun_core.ko` 构建
- `hwrun_core` 基础协议注册表和 ioctl UAPI
- 顶层插件构建编排修复
- 8 个现有用户态插件通过 BUS 启动链验证

当前 `hwrun_core.ko` 提供：

- `/dev/hwrun`
- ABI 版本查询
- ping
- 协议注册
- 协议注销
- 协议解析

它目前是内核边界桥接模块，不是现有 BUS 的强依赖。现有 BUS 继续使用进程内 METAPROTO 路由，这是当前可运行的主路径。

## 7. 已知差异

1. TXT 文档同时描述了自研微内核和 Linux 内核两条路线，当前实现选择 Linux。
2. 文档中许多模块写成 `.ko`，当前 HAP/PMP/FSP/NP/SP/LOADER 主要是用户态 `.so`。
3. 设计中的微内核 IPC 尚未成为当前 BUS 的通信 ABI；当前插件使用 POSIX 运行时。
4. 完整 ISO、rootfs、APT 仓库和 bootloader 流程尚未完成。
5. 参数注入和 Git 状态管理尚未覆盖所有插件。
6. 文档中的协议版本兼容规则还没有完全统一到所有插件。
7. 内核协议路由和用户态 METAPROTO 目前是两套路由表，尚未做统一桥接。

## 8. 后续实施顺序

1. 固化 `minimal` 和 `host` profile 的构建、产物和启动测试。
2. 为 BUS 增加稳定的内核边界客户端，按需访问 `/dev/hwrun`。
3. 统一 `plugin.yml`、`hw_plugin_entry()`、协议版本和错误码。
4. 将 PARAM、LOG、GIT 接口注入现有插件生命周期。
5. 为 HAP/PMP/FSP/NP/LOADER 增加真正的协议调用测试，而不是只测试启动。
6. 对确实需要内核权限的能力增加薄 `.ko`，避免重复实现用户态插件逻辑。
7. 最后再推进 bootloader、rootfs、ISO 和完整发行版构建。

## 9. 基线提交

当前架构基线已推送到 `origin/main`：

```text
bef330b 建立机制最小化内核 profile
7cf3b43 修复现有插件链构建编排
5102ea0 实现 HWRun 内核协议路由基础
```
