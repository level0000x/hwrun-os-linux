# HWRun OS 根目录历史设计 TXT 盘点

盘点日期：2026-09-09
判定基准：`docs/design.md`（v0.3 实现基线 + 0.4 记录）与仓库实际代码目录逐项核对。
说明：
- 根目录 `*.txt` 经 `Get-ChildItem` 精确统计共 36 个：35 份历史设计稿 + 1 个 `CMakeLists.txt`（构建脚本，一并列出但不作为设计稿分组）。
- 35 份设计稿文件名疑似按 15 字符截断（如 `HWRun OS 组件总线（C.txt` 的正文标题实为「HWRun OS 组件总线（Component Bus）完整设计」），下表文件名均为磁盘上的准确文件名，子系统名取自文件内正文标题。
- 各稿头部普遍标注「状态：设计冻结 / 实现语言：C（内核模块）」，描述的是**早期设计意图**；实际代码大量改为用户态 `.so`，因此「已实现」均指仓库现状（多数与文档所述 `.ko` 形态不同）。
- 「文档聚合」：指早期对整个 HWRun OS 的总纲式设计文档，无单一对应实现。

## 1. 汇总表

| TXT 文件 | 对应子系统 | 代码实现位置 | 实现状态 |
| --- | --- | --- | --- |
| `HWRun OS 完整设计文档.txt` | HWRun OS v7.0 总体设计（白皮书级总纲，24 章） | 无单一实现；主线已收敛为 `docs/design.md` | 文档聚合 |
| `HWRun OS 微内核完整设.txt` | Microkernel（自研微内核：地址空间/线程/IPC/插件加载器） | `kernel/` 已改为 Linux kbuild 集成（config profiles、`modules/hwrun_core.c`、`include/uapi/hwrun.h`、`qemu/`），自研微内核已废弃 | 仅设计（路线废弃，机制由 Linux 承担） |
| `HWRun OS 组件总线（C.txt` | BUS（Component Bus 插件管理器） | `bus/src/bus.{c,h}`、`bus/src/loader.c`（dlopen 装载）、`main.c`（PID1/CLI）、`hwlock.{c,h}`、`runtime.c` | 部分实现 |
| `HWRun OS METAPR.txt` | METAPROTO | `bus/src/metaproto.{c,h}` | 已实现 |
| `HWRun OS PARAM.txt` | PARAM | `bus/src/param.{c,h}`（+ `runtime.c` 注入、`yml.c`） | 已实现 |
| `HWRun OS LOG 实现.txt` | LOG | `bus/src/log.{c,h}` | 已实现（bus 内子系统） |
| `HWRun OS GIT 实现.txt` | GIT（系统 git 薄封装） | `git/src/`（executor/branch/history/version/storage/compact/audit） | 已实现 |
| `HWRun OS HAP.txt` | HAP（硬件抽象） | `hap/src/`（hap_cpu/memory/disk/net/system，用户态 `.so`） | 已实现 |
| `HWRun OS PMP 实现.txt` | PMP（进程管理） | `pmp/src/`（pmp_list/proc/task，用户态 `.so`） | 已实现 |
| `HWRun OS FSP 实现.txt` | FSP（文件系统） | `fsp/src/`（file/dir/mount/path/perm，用户态 `.so`） | 已实现 |
| `HWRun OS NP 实现文.txt` | NP（网络） | `np/src/`（socket/conn/interface/resolve，用户态 `.so`） | 已实现 |
| `HWRun OS SP 实现文.txt` | SP（安全） | `sp/src/`（auth/key/hash/crypto/util，用户态 `.so`） | 已实现 |
| `HWRun OS CRYPTO.txt` | CRYPTO（加密原语，SP 子模块） | `crypto/src/`（crypto_core/impl，独立插件 `.so`，基于 OpenSSL） | 已实现 |
| `HWRun OS LOADER.txt` | LOADER（可执行格式加载器） | `loader/src/`（loader_detect/elf/run） | 部分实现 |
| `HWRun OS BUILD.txt` | BUILD（构建/发行版/ISO/APT） | 顶层 `CMakeLists.txt`/`Makefile`、各插件构建、`tests/`、`.github/workflows/ci.yml`；无 ISO/rootfs/APT 仓库产物 | 部分实现 |
| `HWRun OS 引导加载器（.txt` | Bootloader（UEFI + Legacy BIOS） | 无（Linux 路线由 bzImage + QEMU/引导程序承担） | 仅设计未实现 |
| `HWRun OS BOOT 实.txt` | BOOT（引导契约：Boot_Info/微内核移交） | 无对应代码（契约面向已废弃的自研微内核） | 仅设计未实现 |
| `HWRun OS AUDIO.txt` | AUDIO（ALSA 音频） | 无 `audio/` 目录 | 仅设计未实现 |
| `HWRun OS AUDIT.txt` | AUDIT（安全审计协议） | 无 `audit/` 插件目录；仅 `git/src/audit.c`（GIT 插件内的操作审计日志查询） | 仅设计未实现 |
| `HWRun OS CLUSTE.txt` | CLUSTER（集群管理） | 无 | 仅设计未实现 |
| `HWRun OS CONSEN.txt` | CONSENSUS（Raft 共识） | 无 | 仅设计未实现 |
| `HWRun OS COMPRE.txt` | COMPRESS（压缩协议） | 无 | 仅设计未实现 |
| `HWRun OS DISPLA.txt` | DISPLAY（DRM/KMS 显示） | 无 | 仅设计未实现 |
| `HWRun OS DRIVER.txt` | DRIVER（设备驱动框架） | 无 | 仅设计未实现 |
| `HWRun OS FIRMWA.txt` | FIRMWARE（固件管理） | 无 | 仅设计未实现 |
| `HWRun OS FS_TRA.txt` | FS_TRANSFER（跨节点文件传输） | 无 | 仅设计未实现 |
| `HWRun OS HOTPLU.txt` | HOTPLUG（热插拔） | 无 | 仅设计未实现 |
| `HWRun OS INPUT.txt` | INPUT（输入设备） | 无 | 仅设计未实现 |
| `HWRun OS INSTAN.txt` | INSTANCE（容器/MicroVM/沙盒实例） | 无（`runlayers/` 为空占位） | 仅设计未实现 |
| `HWRun OS NODE_D.txt` | NODE_DISCOVERY（节点发现） | 无 | 仅设计未实现 |
| `HWRun OS PERMIS.txt` | PERMISSION（权限/RBAC/LSM） | 无（SP 提供的基础认证/密钥之外无授权协议） | 仅设计未实现 |
| `HWRun OS POWER.txt` | POWER（电源管理） | 无 | 仅设计未实现 |
| `HWRun OS STORAG.txt` | STORAGE（存储后端/卷/快照） | 无 | 仅设计未实现 |
| `HWRun OS TERMIN.txt` | TERMINAL（PTY 终端） | 无 | 仅设计未实现 |
| `HWRun OS 上层应用插件.txt` | SANDBOX / UI / SHELL / CONSOLE / DESKTOP / PKGMGR / MONITOR | 无（`bus/src/main.c` 仅有 CLI 子命令，非上述协议插件） | 仅设计未实现 |
| `CMakeLists.txt` | 顶层构建脚本（非设计稿） | 根 `CMakeLists.txt` | 已实现（构建） |

## 2. 按实现状态分组的明细清单

### 2.1 已实现（10 份设计稿 + CMakeLists.txt 构建脚本，仓库已有对应代码且 design.md 6.x 记录在案）

当前已验证运行链：`METAPROTO → BUS → PARAM → LOG → GIT → HAP → PMP → FSP → NP → SP → CRYPTO → LOADER`（同一 BUS 进程内 8 个插件、13 个协议全部启动成功）。

| TXT | 现状要点 |
| --- | --- |
| `HWRun OS METAPR.txt` | `bus/src/metaproto.{c,h}`：协议注册/注销/解析/列表/订阅/通知/依赖检查/版本兼容；bus 启动时自举，注册表带 `state_file` 持久化路径。 |
| `HWRun OS PARAM.txt` | `bus/src/param.{c,h}`：点分路径参数树、持久化文件 `/var/lib/hwrun/params/state.conf`、Watch 动态通知；0.4 起经 `on_revision` 回调接入 GIT 自动 commit。 |
| `HWRun OS LOG 实现.txt` | `bus/src/log.{c,h}`：DEBUG~FATAL 五级、控制台+文件输出、时间戳/颜色、线程锁；作为 bus 内子系统（非独立插件）。注：异步队列/轮转/串口/审计分离文件等"全量特性"未在代码中出现。 |
| `HWRun OS GIT 实现.txt` | `git/src/`：/usr/bin/git 薄封装（executor/storage/version/branch/history/compact），操作审计写 JSON Lines 审计仓库并由 `audit.c` 查询。 |
| `HWRun OS HAP.txt` | `hap/src/` 用户态 `.so`：CPU/内存/磁盘/网络/系统信息查询（文档原设计为 `.ko`，实际走 `/proc`、`/sys` 等）。 |
| `HWRun OS PMP 实现.txt` | `pmp/src/` 用户态 `.so`：进程列表/进程控制/任务（文档原设计为直接操作 `task_struct` 的内核模块）。 |
| `HWRun OS FSP 实现.txt` | `fsp/src/` 用户态 `.so`：文件/目录/挂载/路径/权限，附 `fsp_test.c`。 |
| `HWRun OS NP 实现文.txt` | `np/src/` 用户态 `.so`：socket/TCP/UDP/UNIX/接口枚举/名称解析，附 `test/test_np.c`。 |
| `HWRun OS SP 实现文.txt` | `sp/src/` 用户态 `.so`：认证、密钥、哈希、AES-256-CBC+HMAC 加密、签名/验签、完整性。 |
| `HWRun OS CRYPTO.txt` | `crypto/` 独立插件 `.so`（requires SP、provides CRYPTO），基于 OpenSSL 而非文档设想的 Linux Kernel Crypto API；hash/HMAC(RFC4231)/AES/RSA 单测覆盖。 |
| `CMakeLists.txt` | 0.3 起 CMake 统一构建（hwrun-core 静态库 + hwrun-bus + 8 插件 + CMocka/ctest）。 |

### 2.2 部分实现（3 份：BUS / LOADER / BUILD）

| TXT | 已实现 | 缺口 |
| --- | --- | --- |
| `HWRun OS 组件总线（C.txt` | 扫描 plugin.yml、dlopen 加载、init→start 生命周期、requires/conflicts 依赖校验、boot_chain 依序启动、stop/unload、协议注册、CLI（`hwrun status/plugins/params/protocols/kctl`）、PID1 常驻。 | install（git clone 安装）、update、replace（运行时热替换）、rollback、clean、副作用记录器（records）、设计中的 `/var/lib/hwrun/git|records` 目录体系未实现。 |
| `HWRun OS LOADER.txt` | 魔数检测 ELF/PE/Mach-O/WASM/APK/Java/脚本；本机 ELF 与 shebang 脚本可 fork+exec 真实运行；`.so` 可 dlopen 驻留。 | 非本机格式（PE/Mach-O/WASM/APK/Java）**只检测不运行**（`loader_run` 返回 `-ENOTSUP`）；文档设想的多格式子插件 + binfmt 机制未实现。 |
| `HWRun OS BUILD.txt` | 源码→插件 `.so`/内核 profile 构建链（Make/CMake）、CI 门禁（.github/workflows/ci.yml）已落地。 | 阶段 4~6 未实现：debootstrap 基础系统、live-build 可启动 ISO、reprepro APT 仓库均无代码/产物。 |
| `HWRun OS 完整设计文档.txt` | — | 文档聚合；其主线由 `docs/design.md` 收敛为当前实现基线（详见 2.4）。 |

### 2.3 仅设计未实现（21 份 TXT，对应第 3.2 节清单中 27 个模块）

以下目录在仓库中均不存在（已逐一核对顶层目录）：微内核（已废弃另列）、引导加载器、BOOT 契约、AUDIO、AUDIT、CLUSTER、CONSENSUS、COMPRESS、DISPLAY、DRIVER、FIRMWARE、FS_TRANSFER、HOTPLUG、INPUT、INSTANCE、NODE_DISCOVERY、PERMISSION、POWER、STORAGE、TERMINAL、SANDBOX、UI、SHELL、CONSOLE、DESKTOP、PKGMGR、MONITOR。详见第 3.2 节清单。

### 2.4 文档聚合 / 历史路线类（2 份）

| TXT | 说明 |
| --- | --- |
| `HWRun OS 完整设计文档.txt` | v7.0「分布式操作系统插件容器」总纲（24 章，含微内核、Git 原生、异构集群、实例拆分、跨地域组网等）。是项目最初"白皮书"，与 `docs/design.md` 是不同代际：本稿仍以"自研微内核 + Git 原生 + 全插件化"为蓝图，design.md 已收敛为"Linux 机制层 + BUS/METAPROTO/PARAM/LOG/GIT + 用户态插件"的可运行基线。 |
| `HWRun OS 微内核完整设.txt` | 自研 i386 微内核完整设计（task/ipc/page 等 ~2500 行构想）。design.md 0.3 明确"废弃自研 i386 微内核，kernel 收敛到 Linux kbuild"。故列为仅设计/已废弃；其"机制与策略分离"思想由 Linux 机制层 + 用户态插件继承。 |

## 3. 重点：设计了但未实现 / 只有部分实现的清单

> 缺口均已先经代码目录核对（仓库无对应目录/源码），非仅凭 design.md。

### 3.1 部分实现类的具体缺口

1. **BUS（`HWRun OS 组件总线（C.txt`）**
   - 设计要点原文简述：BUS 协议接口含 `Scan / Install / Uninstall / Load / Unload / Start / Stop / Replace / Status / List / Resolve / GetDeps / Update / Rollback / Clean`；"安装插件 = git clone"，"替换插件 = 运行时热切换"，全部操作经 Git 自动 commit，卸载时按副作用记录逆向清理。
   - 当前缺口：只实现 Scan/Load(→start)/Stop/Unload/依赖与冲突检查/boot_chain/CLI。Install/Update/Rollback/Clean/Replace 与副作用记录器、`/var/lib/hwrun/git|records` 目录体系均无。
   - 建议落点：bus 层（`bus/src/bus.c` 扩展 + 复用 GIT 协议；或新增独立 install 管理协议插件）。

2. **LOADER（`HWRun OS LOADER.txt`）**
   - 设计要点：Detect/Load/Run/Unload/GetFormats；ELF/PE/COFF/Mach-O/APK/WASM/shebang 多格式子加载器；基于内核 binfmt。
   - 当前缺口：仅本机 ELF + shebang 可真实运行，PE/Mach-O/WASM/APK/Java 停在"能识别、不能跑"。
   - 建议落点：loader 插件（`loader/src/loader_run.c`）逐格式补运行器，或先按文档拆子插件。

3. **BUILD（`HWRun OS BUILD.txt`）**
   - 设计要点：6 阶段——编译内核/内核模块/用户态插件 → debootstrap 基础系统 → live-build ISO → reprepro APT 仓库。
   - 当前缺口：插件与内核构建 + CI 已落地；**可启动 ISO、rootfs、APT 仓库、bootloader 流程**尚未完成（design.md §7.4 亦确认）。
   - 建议落点：构建/发行版层（顶层脚本 + kernel/），按 design.md §8 排最后推进。

### 3.2 仅设计未实现清单（每项：出处 / 设计要点简述 / 缺口 / 建议落点）

| # | 模块 | 出处 TXT | 设计要点（原文简述） | 当前缺口 | 建议落点 |
| --- | --- | --- | --- | --- | --- |
| 1 | MICROKERNEL | `HWRun OS 微内核完整设.txt` | "微内核只提供三样东西：地址空间、线程、IPC……所有其他功能全部在用户态实现"，含启动/内存/线程/IPC/中断/插件加载器 ~2500 行。 | 自研微内核整体废弃（design.md 0.3）；仓库 `kernel/` 为 Linux kbuild 集成 + 薄 `.ko` 桥接。 | 不再自研；内核缺口走"薄 `.ko` 边界桥接"路线（`kernel/modules/hwrun_core.c` 扩展）。 |
| 2 | BOOTLOADER | `HWRun OS 引导加载器（.txt` | x86_64 双启动：UEFI 版 `BOOTX64.EFI`（PE32+）+ Legacy BIOS 版（NASM 两阶段），统一填充 Boot_Info 移交微内核。 | 无任何 bootloader 代码；当前以 Linux bzImage + QEMU/引导程序启动。 | 发行版阶段（kernel/ 或独立 boot/ 目录），design.md §8 排最后。 |
| 3 | BOOT | `HWRun OS BOOT 实.txt` | BOOT 引导协议契约：Boot_Info（内存映射/帧缓冲/模块地址/启动方式/魔数验证）、双启动策略、与微内核入口约定。 | 契约面向自研微内核，Linux 路线无对应消费者；无代码。 | 随 bootloader/发行版一并处理或归档。 |
| 4 | AUDIO | `HWRun OS AUDIO.txt` | AUDIO 协议：ALSA PCM 播放/录音、设备枚举、混音与音量控制、提示音、IPC 消息处理。 | 无 `audio/` 插件，无源码。 | 用户态插件（ALSA/asla-lib），依赖 DRIVER/HAP；设备主线。 |
| 5 | AUDIT | `HWRun OS AUDIT.txt` | AUDIT 协议：安全事件全面覆盖、只追加不可篡改、规则可配、LSM 钩子集成、合规支持（PCI-DSS 等）。 | 无独立 audit 协议插件；仓库仅 GIT 插件自带 `git/src/audit.c`（Git 操作审计查询），非系统安全审计。 | 用户态审计插件（依托 LOG+FSP+SP/GIT），再按需薄 `.ko` 补内核事件。 |
| 6 | CLUSTER | `HWRun OS CLUSTE.txt` | CLUSTER 协议：SWIM/Gossip 成员发现与故障检测、Raft 领导者选举、Git 状态同步、去中心化。 | 无 `cluster/` 插件；NP 已就绪但无上层使用方。 | 用户态插件（基于 NP/GIT），分布式主线。 |
| 7 | CONSENSUS | `HWRun OS CONSEN.txt` | CONSENSUS 协议：Raft 日志复制/多数派提交/成员变更（Joint Consensus）/持久化恢复，基于 libraft。 | 无代码。 | 依赖 CLUSTER/STORAGE 的后续插件。 |
| 8 | COMPRESS | `HWRun OS COMPRE.txt` | COMPRESS 协议：zlib/zstd/xz/lz4 封装、缓冲/文件/流压缩、格式识别；默认 zstd；与 CRYPTO 编排（先压缩后加密等）。 | 无代码；纯用户态、几乎零内核依赖，实现成本最低。 | 用户态服务/库插件（`hwrun-compressd` + 协议），可最先落地。 |
| 9 | DISPLAY | `HWRun OS DISPLA.txt` | DISPLAY 协议：DRM/KMS 资源/连接器/CRTC/帧缓冲/页面翻转，双后端（DRM/KMS + FBDEV）。 | 无代码。 | 设备主线：用户态 libdrm 服务 + 薄 `.ko`，依赖 DRIVER/HAP。 |
| 10 | DRIVER | `HWRun OS DRIVER.txt` | DRIVER 协议：Linux 设备模型（bus/device/driver）、platform/PCI/USB/I2C/SPI、uevent 热插拔、sysfs 拓扑。 | 无代码。 | 薄 `.ko` + 用户态服务（设备主线地基）。 |
| 11 | FIRMWARE | `HWRun OS FIRMWA.txt` | FIRMWARE 协议：efivarfs/libefivar、ESRT、UEFI Capsule、fwupd 集成、Secure Boot 配置。 | 无代码。 | 设备/固件主线（需 UEFI 环境，低优先）。 |
| 12 | FS_TRANSFER | `HWRun OS FS_TRA.txt` | FS_TRANSFER 协议：splice/sendfile 零拷贝、跨节点传输、rsync 增量、断点续传 + 校验和。 | 无代码。 | 用户态插件（基于 FSP+NP+COMPRESS+CRYPTO），分布式主线。 |
| 13 | HOTPLUG | `HWRun OS HOTPLU.txt` | HOTPLUG 协议：接收内核 uevent（netlink）、解析 sysfs、设备分类与策略、优雅拔出。 | 无代码。 | 用户态服务（监听 netlink）+ 薄 `.ko`，设备主线。 |
| 14 | INPUT | `HWRun OS INPUT.txt` | INPUT 协议：Linux input 子系统封装（evdev），键盘/鼠标/触摸板/触摸屏，事件统一、热插拔。 | 无代码。 | 用户态服务（evdev 读取）或薄 `.ko`，设备主线。 |
| 15 | INSTANCE | `HWRun OS INSTAN.txt` | INSTANCE 协议：容器(runc)/系统容器(LXD)/MicroVM(Firecracker)/KVM/Unikernel/gVisor 的统一实例管理层（生命周期/资源/快照/迁移/监控）。 | 无代码；`runlayers/` 仅空占位。 | 用户态插件（封装 runc 等后端 + PMP/FSP/NP），白皮书"实例与拆分"主线。 |
| 16 | NODE_DISCOVERY | `HWRun OS NODE_D.txt` | NODE_DISCOVERY 协议：mDNS(5353)/SSDP/gRPC 自动发现、服务注册、健康检查、去中心化。 | 无代码。 | 用户态插件（基于 NP），分布式主线。 |
| 17 | PERMISSION | `HWRun OS PERMIS.txt` | PERMISSION 协议：混合权限模型（DAC+MAC+Capabilities+RBAC），用户/角色/权限管理，LSM 钩子。 | 无授权协议插件；SP 目前仅覆盖认证/密钥等基础能力。 | 用户态 RBAC 服务（挂 SP/CRYPTO 之上），安全主线。 |
| 18 | POWER | `HWRun OS POWER.txt` | POWER 协议：PM/ACPI 状态机（s0~s5）、power_supply 电池、thermal、cpufreq 策略，关机/重启授权。 | 无代码。 | 设备主线用户态服务，依赖 DRIVER/HAP，低优先。 |
| 19 | STORAGE | `HWRun OS STORAG.txt` | STORAGE 协议：存储设备→池→卷→快照→备份分层、thin provisioning、多后端（dm/LVM/SCSI/NVMe）、热插拔。 | 无代码（FSP 只做文件系统层）。 | 用户态卷管理服务 + 薄 `.ko`/系统工具（LVM/dm），系统服务主线。 |
| 20 | TERMINAL | `HWRun OS TERMIN.txt` | TERMINAL 协议：PTY 会话（posix_openpt/termios/TIOCSWINSZ）、行规程、VT100/xterm 仿真。 | 无代码。 | 用户态插件（纯 POSIX PTY），交互层地基；被 SHELL/CONSOLE 依赖。 |
| 21 | SANDBOX | `HWRun OS 上层应用插件.txt`（二） | SANDBOX：namespace + seccomp-bpf 进程级隔离，基于 nsjail/gVisor 的轻量封装，短期运行不受信代码。 | 无代码。 | 用户态（依赖 PMP/FSP），与 INSTANCE 区分定位。 |
| 22 | UI | `HWRun OS 上层应用插件.txt`（三） | UI：窗口/事件/渲染管理，TUI 优先。 | 无代码。 | 交互层（依赖 TERMINAL/INPUT/DISPLAY）。 |
| 23 | SHELL | `HWRun OS 上层应用插件.txt`（四） | SHELL：命令行解释器。 | 无代码（bus 的 `hwrun` CLI 非协议插件）。 | 交互层（依赖 TERMINAL/PMP/LOADER）。 |
| 24 | CONSOLE | `HWRun OS 上层应用插件.txt`（五） | CONSOLE：系统控制台。 | 无代码。 | 交互层。 |
| 25 | DESKTOP | `HWRun OS 上层应用插件.txt`（六） | DESKTOP：桌面环境（三栏布局）。 | 无代码。 | UI 之后。 |
| 26 | PKGMGR | `HWRun OS 上层应用插件.txt`（七） | PKGMGR：包管理（插件即包，Git 原生分发）。 | 无代码（与 BUILD 的 APT 仓库相配套）。 | 依赖 STORAGE/GIT/LOADER + APT 仓库。 |
| 27 | MONITOR | `HWRun OS 上层应用插件.txt`（八） | MONITOR：系统监控（基于 HAP/PMP/NP/STORAGE 采集展示）。 | 无代码。 | 上层（依赖各协议采集点就绪）。 |

## 4. 未实现项粗优先级排序（结合启动顺序与白皮书主线）

排序依据：
- **启动链延伸**：哪些最贴近已跑通的 `METAPROTO→…→LOADER` 链、可直接进 BUS 作为"其他插件"；
- **用户态可行性**：越少依赖 root/内核钩子越先做（design.md §8.6 也要求先薄 `.ko` 后发行版）；
- **白皮书主线**（`HWRun OS 完整设计文档.txt` 目录序）：可运行格式 → 实例与拆分 → 权限与安全 → 用户界面 → 构建与分发；
- design.md §7/§8 明确：ISO/rootfs/APT/bootloader 放最后，先补需内核权限的薄 `.ko`。

| 优先级 | 项目 | 理由 |
| --- | --- | --- |
| P0（最先） | COMPRESS；PERMISSION（用户态 RBAC 部分）；STORAGE 的用户态卷管理层；AUDIT 的用户态事件审计 | 纯/近用户态、无强内核依赖，处于系统服务层，是"其他插件"的第一批自然落点；PERMISSION/AUDIT/STORAGE 紧贴白皮书"权限与安全""存储"主线且多份设计稿自述加载于上层插件之前。 |
| P1 | INSTANCE + SANDBOX（容器/沙盒）；TERMINAL + SHELL + CONSOLE | 白皮书主线"实例与拆分""用户界面"交汇处；INSTANCE/SANDBOX 用 runc/namespace+seccomp 用户态封装（需 root），SHELL/CONSOLE 使系统具备可用交互，可端到端验证整条插件链。 |
| P2 | CLUSTER / CONSENSUS / NODE_DISCOVERY / FS_TRANSFER（分布式组） | 白皮书"异构集群/分布式原生/跨地域组网"主线；NP 已实现，具备前提，但依赖 P0 的 STORAGE/COMPRESS 与 CRYPTO 编排，放中后期。 |
| P3 | DRIVER / INPUT / DISPLAY / AUDIO / POWER / HOTPLUG / FIRMWARE（设备组） | 大多需真实硬件与 root/内核钩子；design.md §8.6 建议按需补薄 `.ko`，作为后续发行版/硬件目标的支持层。 |
| P4（最后） | UI / DESKTOP / PKGMGR / MONITOR；BOOTLOADER + BOOT 契约 + BUILD 的 ISO/rootfs/APT 仓库 | 桌面与包管理依赖前述交互层/存储与 APT 仓库；引导与发行版按 design.md §7.4/§8.8 明确排在"最后推进"，需 linux-src、debootstrap/live-build/reprepro 等外部工具链。 |

备注：`微内核完整设` 无需排期（路线已废弃）；`完整设计文档` 为总纲，后续新实现均应回填/对齐到 `docs/design.md`（在 5.3 扩展协议区勾销进度），避免与根目录 TXT 的双轨文档分叉。
