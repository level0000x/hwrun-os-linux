cmd_/mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/hwrun_core.o := gcc -Wp,-MMD,/mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/.hwrun_core.o.d -nostdinc -I/root/linux-src/arch/x86/include -I./arch/x86/include/generated -I/root/linux-src/include -I./include -I/root/linux-src/arch/x86/include/uapi -I./arch/x86/include/generated/uapi -I/root/linux-src/include/uapi -I./include/generated/uapi -include /root/linux-src/include/linux/compiler-version.h -include /root/linux-src/include/linux/kconfig.h -include /root/linux-src/include/linux/compiler_types.h -D__KERNEL__ -fmacro-prefix-map=/root/linux-src/= -Wall -Wundef -Werror=strict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -fshort-wchar -fno-PIE -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type -Wno-format-security -std=gnu11 -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx -fcf-protection=none -m64 -falign-jumps=1 -falign-loops=1 -mno-80387 -mno-fp-ret-in-387 -mpreferred-stack-boundary=3 -mskip-rax-setup -mtune=generic -mno-red-zone -mcmodel=kernel -Wno-sign-compare -fno-asynchronous-unwind-tables -mindirect-branch=thunk-extern -mindirect-branch-register -mindirect-branch-cs-prefix -mfunction-return=thunk-extern -fno-jump-tables -fno-delete-null-pointer-checks -Wno-frame-address -Wno-format-truncation -Wno-format-overflow -Wno-address-of-packed-member -O2 -fno-allow-store-data-races -Wframe-larger-than=2048 -fstack-protector-strong -Wno-main -Wno-unused-but-set-variable -Wno-unused-const-variable -fomit-frame-pointer -fno-stack-clash-protection -Wdeclaration-after-statement -Wvla -Wno-pointer-sign -Wcast-function-type -Wno-stringop-truncation -Wno-stringop-overflow -Wno-restrict -Wno-maybe-uninitialized -Werror -Wno-alloc-size-larger-than -Wimplicit-fallthrough=5 -fno-strict-overflow -fno-stack-check -fconserve-stack -Werror=date-time -Werror=incompatible-pointer-types -Werror=designated-init -Wno-packed-not-aligned -I/mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/../include  -DMODULE  -DKBUILD_BASENAME='"hwrun_core"' -DKBUILD_MODNAME='"hwrun_core"' -D__KBUILD_MODNAME=kmod_hwrun_core -c -o /mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/hwrun_core.o /mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/hwrun_core.c   ; ./tools/objtool/objtool --hacks=jump_label --hacks=noinstr --orc --retpoline --rethunk --static-call --uaccess   --module /mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/hwrun_core.o

source_/mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/hwrun_core.o := /mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/hwrun_core.c

deps_/mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/hwrun_core.o := \
    $(wildcard include/config/COMPAT) \
  /root/linux-src/include/linux/compiler-version.h \
    $(wildcard include/config/CC_VERSION_TEXT) \
  /root/linux-src/include/linux/kconfig.h \
    $(wildcard include/config/CPU_BIG_ENDIAN) \
    $(wildcard include/config/BOOGER) \
    $(wildcard include/config/FOO) \
  /root/linux-src/include/linux/compiler_types.h \
    $(wildcard include/config/DEBUG_INFO_BTF) \
    $(wildcard include/config/PAHOLE_HAS_BTF_TAG) \
    $(wildcard include/config/HAVE_ARCH_COMPILER_H) \
    $(wildcard include/config/CC_HAS_ASM_INLINE) \
  /root/linux-src/include/linux/compiler_attributes.h \
  /root/linux-src/include/linux/compiler-gcc.h \
    $(wildcard include/config/RETPOLINE) \
    $(wildcard include/config/ARCH_USE_BUILTIN_BSWAP) \
    $(wildcard include/config/SHADOW_CALL_STACK) \
    $(wildcard include/config/KCOV) \
  /root/linux-src/include/linux/fs.h \
    $(wildcard include/config/READ_ONLY_THP_FOR_FS) \
    $(wildcard include/config/SMP) \
    $(wildcard include/config/FS_POSIX_ACL) \
    $(wildcard include/config/SECURITY) \
    $(wildcard include/config/CGROUP_WRITEBACK) \
    $(wildcard include/config/IMA) \
    $(wildcard include/config/FILE_LOCKING) \
    $(wildcard include/config/FSNOTIFY) \
    $(wildcard include/config/FS_ENCRYPTION) \
    $(wildcard include/config/FS_VERITY) \
    $(wildcard include/config/PREEMPTION) \
    $(wildcard include/config/EPOLL) \
    $(wildcard include/config/UNICODE) \
    $(wildcard include/config/MMU) \
    $(wildcard include/config/QUOTA) \
    $(wildcard include/config/FS_DAX) \
    $(wildcard include/config/BLOCK) \
    $(wildcard include/config/DEBUG_LOCK_ALLOC) \
  /root/linux-src/include/linux/linkage.h \
    $(wildcard include/config/ARCH_USE_SYM_ANNOTATIONS) \
  /root/linux-src/include/linux/compiler_types.h \
  /root/linux-src/include/linux/stringify.h \
  /root/linux-src/include/linux/export.h \
    $(wildcard include/config/MODVERSIONS) \
    $(wildcard include/config/HAVE_ARCH_PREL32_RELOCATIONS) \
    $(wildcard include/config/MODULES) \
    $(wildcard include/config/TRIM_UNUSED_KSYMS) \
  /root/linux-src/include/linux/compiler.h \
    $(wildcard include/config/TRACE_BRANCH_PROFILING) \
    $(wildcard include/config/PROFILE_ALL_BRANCHES) \
    $(wildcard include/config/OBJTOOL) \
  arch/x86/include/generated/asm/rwonce.h \
  /root/linux-src/include/asm-generic/rwonce.h \
  /root/linux-src/include/linux/kasan-checks.h \
    $(wildcard include/config/KASAN_GENERIC) \
    $(wildcard include/config/KASAN_SW_TAGS) \
  /root/linux-src/include/linux/types.h \
    $(wildcard include/config/HAVE_UID16) \
    $(wildcard include/config/UID16) \
    $(wildcard include/config/ARCH_DMA_ADDR_T_64BIT) \
    $(wildcard include/config/PHYS_ADDR_T_64BIT) \
    $(wildcard include/config/64BIT) \
    $(wildcard include/config/ARCH_32BIT_USTAT_F_TINODE) \
  /root/linux-src/include/uapi/linux/types.h \
  arch/x86/include/generated/uapi/asm/types.h \
  /root/linux-src/include/uapi/asm-generic/types.h \
  /root/linux-src/include/asm-generic/int-ll64.h \
  /root/linux-src/include/uapi/asm-generic/int-ll64.h \
  /root/linux-src/arch/x86/include/uapi/asm/bitsperlong.h \
  /root/linux-src/include/asm-generic/bitsperlong.h \
  /root/linux-src/include/uapi/asm-generic/bitsperlong.h \
  /root/linux-src/include/uapi/linux/posix_types.h \
  /root/linux-src/include/linux/stddef.h \
  /root/linux-src/include/uapi/linux/stddef.h \
  /root/linux-src/arch/x86/include/asm/posix_types.h \
    $(wildcard include/config/X86_32) \
  /root/linux-src/arch/x86/include/uapi/asm/posix_types_64.h \
  /root/linux-src/include/uapi/asm-generic/posix_types.h \
  /root/linux-src/include/linux/kcsan-checks.h \
    $(wildcard include/config/KCSAN) \
    $(wildcard include/config/KCSAN_WEAK_MEMORY) \
    $(wildcard include/config/KCSAN_IGNORE_ATOMICS) \
  /root/linux-src/arch/x86/include/asm/linkage.h \
    $(wildcard include/config/X86_64) \
    $(wildcard include/config/X86_ALIGNMENT_16) \
    $(wildcard include/config/RETHUNK) \
    $(wildcard include/config/SLS) \
  /root/linux-src/arch/x86/include/asm/ibt.h \
    $(wildcard include/config/X86_KERNEL_IBT) \
  /root/linux-src/include/linux/wait_bit.h \
  /root/linux-src/include/linux/wait.h \
    $(wildcard include/config/LOCKDEP) \
  /root/linux-src/include/linux/list.h \
    $(wildcard include/config/DEBUG_LIST) \
  /root/linux-src/include/linux/container_of.h \
  /root/linux-src/include/linux/build_bug.h \
  /root/linux-src/include/linux/err.h \
  arch/x86/include/generated/uapi/asm/errno.h \
  /root/linux-src/include/uapi/asm-generic/errno.h \
  /root/linux-src/include/uapi/asm-generic/errno-base.h \
  /root/linux-src/include/linux/poison.h \
    $(wildcard include/config/ILLEGAL_POINTER_VALUE) \
  /root/linux-src/include/linux/const.h \
  /root/linux-src/include/vdso/const.h \
  /root/linux-src/include/uapi/linux/const.h \
  /root/linux-src/arch/x86/include/asm/barrier.h \
  /root/linux-src/arch/x86/include/asm/alternative.h \
  /root/linux-src/arch/x86/include/asm/asm.h \
    $(wildcard include/config/KPROBES) \
  /root/linux-src/arch/x86/include/asm/extable_fixup_types.h \
  /root/linux-src/arch/x86/include/asm/nops.h \
  /root/linux-src/include/asm-generic/barrier.h \
  /root/linux-src/include/linux/spinlock.h \
    $(wildcard include/config/DEBUG_SPINLOCK) \
    $(wildcard include/config/PREEMPT_RT) \
  /root/linux-src/include/linux/typecheck.h \
  /root/linux-src/include/linux/preempt.h \
    $(wildcard include/config/PREEMPT_COUNT) \
    $(wildcard include/config/DEBUG_PREEMPT) \
    $(wildcard include/config/TRACE_PREEMPT_TOGGLE) \
    $(wildcard include/config/PREEMPT_NOTIFIERS) \
  /root/linux-src/arch/x86/include/asm/preempt.h \
    $(wildcard include/config/PREEMPT_DYNAMIC) \
  /root/linux-src/arch/x86/include/asm/rmwcc.h \
  /root/linux-src/arch/x86/include/asm/percpu.h \
    $(wildcard include/config/X86_64_SMP) \
    $(wildcard include/config/X86_CMPXCHG64) \
  /root/linux-src/include/linux/kernel.h \
    $(wildcard include/config/PREEMPT_VOLUNTARY_BUILD) \
    $(wildcard include/config/HAVE_PREEMPT_DYNAMIC_CALL) \
    $(wildcard include/config/HAVE_PREEMPT_DYNAMIC_KEY) \
    $(wildcard include/config/PREEMPT_) \
    $(wildcard include/config/DEBUG_ATOMIC_SLEEP) \
    $(wildcard include/config/PROVE_LOCKING) \
    $(wildcard include/config/TRACING) \
    $(wildcard include/config/FTRACE_MCOUNT_RECORD) \
  /root/linux-src/include/linux/stdarg.h \
  /root/linux-src/include/linux/align.h \
  /root/linux-src/include/linux/limits.h \
  /root/linux-src/include/uapi/linux/limits.h \
  /root/linux-src/include/vdso/limits.h \
  /root/linux-src/include/linux/bitops.h \
  /root/linux-src/include/linux/bits.h \
  /root/linux-src/include/vdso/bits.h \
  /root/linux-src/include/uapi/linux/kernel.h \
  /root/linux-src/include/uapi/linux/sysinfo.h \
  /root/linux-src/include/asm-generic/bitops/generic-non-atomic.h \
  /root/linux-src/arch/x86/include/asm/bitops.h \
    $(wildcard include/config/X86_CMOV) \
  /root/linux-src/include/asm-generic/bitops/sched.h \
  /root/linux-src/arch/x86/include/asm/arch_hweight.h \
  /root/linux-src/arch/x86/include/asm/cpufeatures.h \
  /root/linux-src/arch/x86/include/asm/required-features.h \
    $(wildcard include/config/X86_MINIMUM_CPU_FAMILY) \
    $(wildcard include/config/MATH_EMULATION) \
    $(wildcard include/config/X86_PAE) \
    $(wildcard include/config/X86_P6_NOP) \
    $(wildcard include/config/MATOM) \
    $(wildcard include/config/PARAVIRT_XXL) \
  /root/linux-src/arch/x86/include/asm/disabled-features.h \
    $(wildcard include/config/X86_UMIP) \
    $(wildcard include/config/X86_INTEL_MEMORY_PROTECTION_KEYS) \
    $(wildcard include/config/X86_5LEVEL) \
    $(wildcard include/config/PAGE_TABLE_ISOLATION) \
    $(wildcard include/config/CPU_UNRET_ENTRY) \
    $(wildcard include/config/INTEL_IOMMU_SVM) \
    $(wildcard include/config/X86_SGX) \
    $(wildcard include/config/INTEL_TDX_GUEST) \
  /root/linux-src/include/asm-generic/bitops/const_hweight.h \
  /root/linux-src/include/asm-generic/bitops/instrumented-atomic.h \
  /root/linux-src/include/linux/instrumented.h \
  /root/linux-src/include/linux/kmsan-checks.h \
    $(wildcard include/config/KMSAN) \
  /root/linux-src/include/asm-generic/bitops/instrumented-non-atomic.h \
    $(wildcard include/config/KCSAN_ASSUME_PLAIN_WRITES_ATOMIC) \
  /root/linux-src/include/asm-generic/bitops/instrumented-lock.h \
  /root/linux-src/include/asm-generic/bitops/le.h \
  /root/linux-src/arch/x86/include/uapi/asm/byteorder.h \
  /root/linux-src/include/linux/byteorder/little_endian.h \
  /root/linux-src/include/uapi/linux/byteorder/little_endian.h \
  /root/linux-src/include/linux/swab.h \
  /root/linux-src/include/uapi/linux/swab.h \
  /root/linux-src/arch/x86/include/uapi/asm/swab.h \
  /root/linux-src/include/linux/byteorder/generic.h \
  /root/linux-src/include/asm-generic/bitops/ext2-atomic-setbit.h \
  /root/linux-src/include/linux/kstrtox.h \
  /root/linux-src/include/linux/log2.h \
    $(wildcard include/config/ARCH_HAS_ILOG2_U32) \
    $(wildcard include/config/ARCH_HAS_ILOG2_U64) \
  /root/linux-src/include/linux/math.h \
  /root/linux-src/arch/x86/include/asm/div64.h \
  /root/linux-src/include/asm-generic/div64.h \
  /root/linux-src/include/linux/minmax.h \
  /root/linux-src/include/linux/panic.h \
    $(wildcard include/config/PANIC_TIMEOUT) \
  /root/linux-src/include/linux/printk.h \
    $(wildcard include/config/MESSAGE_LOGLEVEL_DEFAULT) \
    $(wildcard include/config/CONSOLE_LOGLEVEL_DEFAULT) \
    $(wildcard include/config/CONSOLE_LOGLEVEL_QUIET) \
    $(wildcard include/config/EARLY_PRINTK) \
    $(wildcard include/config/PRINTK) \
    $(wildcard include/config/PRINTK_INDEX) \
    $(wildcard include/config/DYNAMIC_DEBUG) \
    $(wildcard include/config/DYNAMIC_DEBUG_CORE) \
  /root/linux-src/include/linux/init.h \
    $(wildcard include/config/STRICT_KERNEL_RWX) \
    $(wildcard include/config/STRICT_MODULE_RWX) \
    $(wildcard include/config/LTO_CLANG) \
  /root/linux-src/include/linux/kern_levels.h \
  /root/linux-src/include/linux/ratelimit_types.h \
  /root/linux-src/include/uapi/linux/param.h \
  arch/x86/include/generated/uapi/asm/param.h \
  /root/linux-src/include/asm-generic/param.h \
    $(wildcard include/config/HZ) \
  /root/linux-src/include/uapi/asm-generic/param.h \
  /root/linux-src/include/linux/spinlock_types_raw.h \
  /root/linux-src/arch/x86/include/asm/spinlock_types.h \
  /root/linux-src/include/asm-generic/qspinlock_types.h \
    $(wildcard include/config/NR_CPUS) \
  /root/linux-src/include/asm-generic/qrwlock_types.h \
  /root/linux-src/include/linux/lockdep_types.h \
    $(wildcard include/config/PROVE_RAW_LOCK_NESTING) \
    $(wildcard include/config/LOCK_STAT) \
  /root/linux-src/include/linux/once_lite.h \
  /root/linux-src/include/linux/static_call_types.h \
    $(wildcard include/config/HAVE_STATIC_CALL) \
    $(wildcard include/config/HAVE_STATIC_CALL_INLINE) \
  /root/linux-src/include/linux/instruction_pointer.h \
  /root/linux-src/include/asm-generic/percpu.h \
    $(wildcard include/config/HAVE_SETUP_PER_CPU_AREA) \
  /root/linux-src/include/linux/threads.h \
    $(wildcard include/config/BASE_SMALL) \
  /root/linux-src/include/linux/percpu-defs.h \
    $(wildcard include/config/DEBUG_FORCE_WEAK_PER_CPU) \
    $(wildcard include/config/AMD_MEM_ENCRYPT) \
  /root/linux-src/include/linux/thread_info.h \
    $(wildcard include/config/THREAD_INFO_IN_TASK) \
    $(wildcard include/config/GENERIC_ENTRY) \
    $(wildcard include/config/HAVE_ARCH_WITHIN_STACK_FRAMES) \
    $(wildcard include/config/HARDENED_USERCOPY) \
    $(wildcard include/config/BUG) \
  /root/linux-src/include/linux/bug.h \
    $(wildcard include/config/GENERIC_BUG) \
    $(wildcard include/config/BUG_ON_DATA_CORRUPTION) \
  /root/linux-src/arch/x86/include/asm/bug.h \
    $(wildcard include/config/DEBUG_BUGVERBOSE) \
  /root/linux-src/include/linux/instrumentation.h \
    $(wildcard include/config/NOINSTR_VALIDATION) \
  /root/linux-src/include/linux/objtool.h \
    $(wildcard include/config/FRAME_POINTER) \
  /root/linux-src/include/asm-generic/bug.h \
    $(wildcard include/config/GENERIC_BUG_RELATIVE_POINTERS) \
  /root/linux-src/include/linux/restart_block.h \
  /root/linux-src/include/linux/time64.h \
  /root/linux-src/include/linux/math64.h \
    $(wildcard include/config/ARCH_SUPPORTS_INT128) \
  /root/linux-src/include/vdso/math64.h \
  /root/linux-src/include/vdso/time64.h \
  /root/linux-src/include/uapi/linux/time.h \
  /root/linux-src/include/uapi/linux/time_types.h \
  /root/linux-src/include/linux/errno.h \
  /root/linux-src/include/uapi/linux/errno.h \
  /root/linux-src/arch/x86/include/asm/current.h \
  /root/linux-src/arch/x86/include/asm/thread_info.h \
    $(wildcard include/config/VM86) \
    $(wildcard include/config/X86_IOPL_IOPERM) \
    $(wildcard include/config/IA32_EMULATION) \
  /root/linux-src/arch/x86/include/asm/page.h \
  /root/linux-src/arch/x86/include/asm/page_types.h \
    $(wildcard include/config/PHYSICAL_START) \
    $(wildcard include/config/PHYSICAL_ALIGN) \
    $(wildcard include/config/DYNAMIC_PHYSICAL_MASK) \
  /root/linux-src/include/linux/mem_encrypt.h \
    $(wildcard include/config/ARCH_HAS_MEM_ENCRYPT) \
  /root/linux-src/arch/x86/include/asm/mem_encrypt.h \
  /root/linux-src/include/linux/cc_platform.h \
    $(wildcard include/config/ARCH_HAS_CC_PLATFORM) \
  /root/linux-src/arch/x86/include/uapi/asm/bootparam.h \
  /root/linux-src/include/linux/screen_info.h \
  /root/linux-src/include/uapi/linux/screen_info.h \
  /root/linux-src/include/linux/apm_bios.h \
  /root/linux-src/include/uapi/linux/apm_bios.h \
  /root/linux-src/include/uapi/linux/ioctl.h \
  arch/x86/include/generated/uapi/asm/ioctl.h \
  /root/linux-src/include/asm-generic/ioctl.h \
  /root/linux-src/include/uapi/asm-generic/ioctl.h \
  /root/linux-src/include/linux/edd.h \
  /root/linux-src/include/uapi/linux/edd.h \
  /root/linux-src/arch/x86/include/asm/ist.h \
  /root/linux-src/arch/x86/include/uapi/asm/ist.h \
  /root/linux-src/include/video/edid.h \
    $(wildcard include/config/X86) \
  /root/linux-src/include/uapi/video/edid.h \
  /root/linux-src/arch/x86/include/asm/page_64_types.h \
    $(wildcard include/config/KASAN) \
    $(wildcard include/config/DYNAMIC_MEMORY_LAYOUT) \
    $(wildcard include/config/RANDOMIZE_BASE) \
  /root/linux-src/arch/x86/include/asm/kaslr.h \
    $(wildcard include/config/RANDOMIZE_MEMORY) \
  /root/linux-src/arch/x86/include/asm/page_64.h \
    $(wildcard include/config/DEBUG_VIRTUAL) \
    $(wildcard include/config/FLATMEM) \
    $(wildcard include/config/X86_VSYSCALL_EMULATION) \
  /root/linux-src/include/linux/range.h \
  /root/linux-src/include/asm-generic/memory_model.h \
    $(wildcard include/config/SPARSEMEM_VMEMMAP) \
    $(wildcard include/config/SPARSEMEM) \
  /root/linux-src/include/linux/pfn.h \
  /root/linux-src/include/asm-generic/getorder.h \
  /root/linux-src/arch/x86/include/asm/cpufeature.h \
    $(wildcard include/config/X86_FEATURE_NAMES) \
  /root/linux-src/arch/x86/include/asm/processor.h \
    $(wildcard include/config/X86_VSMP) \
    $(wildcard include/config/X86_VMX_FEATURE_NAMES) \
    $(wildcard include/config/STACKPROTECTOR) \
    $(wildcard include/config/X86_DEBUGCTLMSR) \
    $(wildcard include/config/CPU_SUP_AMD) \
    $(wildcard include/config/XEN) \
  /root/linux-src/arch/x86/include/asm/processor-flags.h \
  /root/linux-src/arch/x86/include/uapi/asm/processor-flags.h \
  /root/linux-src/arch/x86/include/asm/math_emu.h \
  /root/linux-src/arch/x86/include/asm/ptrace.h \
    $(wildcard include/config/PARAVIRT) \
  /root/linux-src/arch/x86/include/asm/segment.h \
    $(wildcard include/config/XEN_PV) \
  /root/linux-src/arch/x86/include/asm/cache.h \
    $(wildcard include/config/X86_L1_CACHE_SHIFT) \
    $(wildcard include/config/X86_INTERNODE_CACHE_SHIFT) \
  /root/linux-src/arch/x86/include/uapi/asm/ptrace.h \
  /root/linux-src/arch/x86/include/uapi/asm/ptrace-abi.h \
  /root/linux-src/arch/x86/include/asm/paravirt_types.h \
    $(wildcard include/config/PGTABLE_LEVELS) \
    $(wildcard include/config/ZERO_CALL_USED_REGS) \
    $(wildcard include/config/PARAVIRT_DEBUG) \
  /root/linux-src/arch/x86/include/asm/desc_defs.h \
  /root/linux-src/arch/x86/include/asm/pgtable_types.h \
    $(wildcard include/config/MEM_SOFT_DIRTY) \
    $(wildcard include/config/HAVE_ARCH_USERFAULTFD_WP) \
    $(wildcard include/config/PROC_FS) \
  /root/linux-src/arch/x86/include/asm/pgtable_64_types.h \
    $(wildcard include/config/DEBUG_KMAP_LOCAL_FORCE_MAP) \
  /root/linux-src/arch/x86/include/asm/sparsemem.h \
    $(wildcard include/config/NUMA_KEEP_MEMINFO) \
  /root/linux-src/arch/x86/include/asm/nospec-branch.h \
    $(wildcard include/config/DEBUG_ENTRY) \
    $(wildcard include/config/CPU_IBPB_ENTRY) \
  /root/linux-src/include/linux/static_key.h \
  /root/linux-src/include/linux/jump_label.h \
    $(wildcard include/config/JUMP_LABEL) \
    $(wildcard include/config/HAVE_ARCH_JUMP_LABEL_RELATIVE) \
  /root/linux-src/arch/x86/include/asm/jump_label.h \
    $(wildcard include/config/HAVE_JUMP_LABEL_HACK) \
  /root/linux-src/arch/x86/include/asm/msr-index.h \
  /root/linux-src/arch/x86/include/asm/unwind_hints.h \
  /root/linux-src/arch/x86/include/asm/orc_types.h \
  /root/linux-src/arch/x86/include/asm/GEN-for-each-reg.h \
  /root/linux-src/arch/x86/include/asm/proto.h \
  /root/linux-src/arch/x86/include/uapi/asm/ldt.h \
  /root/linux-src/arch/x86/include/uapi/asm/sigcontext.h \
  /root/linux-src/arch/x86/include/asm/msr.h \
    $(wildcard include/config/TRACEPOINTS) \
  /root/linux-src/arch/x86/include/asm/msr-index.h \
  /root/linux-src/arch/x86/include/asm/cpumask.h \
  /root/linux-src/include/linux/cpumask.h \
    $(wildcard include/config/FORCE_NR_CPUS) \
    $(wildcard include/config/HOTPLUG_CPU) \
    $(wildcard include/config/DEBUG_PER_CPU_MAPS) \
    $(wildcard include/config/CPUMASK_OFFSTACK) \
  /root/linux-src/include/linux/bitmap.h \
  /root/linux-src/include/linux/find.h \
  /root/linux-src/include/linux/string.h \
    $(wildcard include/config/BINARY_PRINTF) \
    $(wildcard include/config/FORTIFY_SOURCE) \
  /root/linux-src/include/uapi/linux/string.h \
  /root/linux-src/arch/x86/include/asm/string.h \
  /root/linux-src/arch/x86/include/asm/string_64.h \
    $(wildcard include/config/ARCH_HAS_UACCESS_FLUSHCACHE) \
  /root/linux-src/include/linux/atomic.h \
  /root/linux-src/arch/x86/include/asm/atomic.h \
  /root/linux-src/arch/x86/include/asm/cmpxchg.h \
  /root/linux-src/arch/x86/include/asm/cmpxchg_64.h \
  /root/linux-src/arch/x86/include/asm/atomic64_64.h \
  /root/linux-src/include/linux/atomic/atomic-arch-fallback.h \
    $(wildcard include/config/GENERIC_ATOMIC64) \
  /root/linux-src/include/linux/atomic/atomic-long.h \
  /root/linux-src/include/linux/atomic/atomic-instrumented.h \
  /root/linux-src/include/linux/gfp_types.h \
    $(wildcard include/config/KASAN_HW_TAGS) \
  /root/linux-src/include/linux/numa.h \
    $(wildcard include/config/NODES_SHIFT) \
    $(wildcard include/config/NUMA) \
    $(wildcard include/config/HAVE_ARCH_NODE_DEV_GROUP) \
  /root/linux-src/arch/x86/include/uapi/asm/msr.h \
  /root/linux-src/arch/x86/include/asm/shared/msr.h \
  /root/linux-src/include/linux/tracepoint-defs.h \
  /root/linux-src/arch/x86/include/asm/special_insns.h \
  /root/linux-src/include/linux/irqflags.h \
    $(wildcard include/config/TRACE_IRQFLAGS) \
    $(wildcard include/config/IRQSOFF_TRACER) \
    $(wildcard include/config/PREEMPT_TRACER) \
    $(wildcard include/config/DEBUG_IRQFLAGS) \
    $(wildcard include/config/TRACE_IRQFLAGS_SUPPORT) \
  /root/linux-src/arch/x86/include/asm/irqflags.h \
  /root/linux-src/arch/x86/include/asm/fpu/types.h \
  /root/linux-src/arch/x86/include/asm/vmxfeatures.h \
  /root/linux-src/arch/x86/include/asm/vdso/processor.h \
  /root/linux-src/include/linux/personality.h \
  /root/linux-src/include/uapi/linux/personality.h \
  /root/linux-src/include/linux/cache.h \
    $(wildcard include/config/ARCH_HAS_CACHE_LINE_SIZE) \
  /root/linux-src/include/linux/bottom_half.h \
  /root/linux-src/include/linux/lockdep.h \
    $(wildcard include/config/DEBUG_LOCKING_API_SELFTESTS) \
  /root/linux-src/include/linux/smp.h \
    $(wildcard include/config/UP_LATE_INIT) \
  /root/linux-src/include/linux/smp_types.h \
  /root/linux-src/include/linux/llist.h \
    $(wildcard include/config/ARCH_HAVE_NMI_SAFE_CMPXCHG) \
  /root/linux-src/arch/x86/include/asm/smp.h \
    $(wildcard include/config/X86_LOCAL_APIC) \
    $(wildcard include/config/DEBUG_NMI_SELFTEST) \
  arch/x86/include/generated/asm/mmiowb.h \
  /root/linux-src/include/asm-generic/mmiowb.h \
    $(wildcard include/config/MMIOWB) \
  /root/linux-src/include/linux/spinlock_types.h \
  /root/linux-src/include/linux/rwlock_types.h \
  /root/linux-src/arch/x86/include/asm/spinlock.h \
  /root/linux-src/arch/x86/include/asm/paravirt.h \
    $(wildcard include/config/PARAVIRT_SPINLOCKS) \
  /root/linux-src/arch/x86/include/asm/frame.h \
  /root/linux-src/arch/x86/include/asm/qspinlock.h \
  /root/linux-src/include/asm-generic/qspinlock.h \
  /root/linux-src/arch/x86/include/asm/qrwlock.h \
  /root/linux-src/include/asm-generic/qrwlock.h \
  /root/linux-src/include/linux/rwlock.h \
    $(wildcard include/config/PREEMPT) \
  /root/linux-src/include/linux/spinlock_api_smp.h \
    $(wildcard include/config/INLINE_SPIN_LOCK) \
    $(wildcard include/config/INLINE_SPIN_LOCK_BH) \
    $(wildcard include/config/INLINE_SPIN_LOCK_IRQ) \
    $(wildcard include/config/INLINE_SPIN_LOCK_IRQSAVE) \
    $(wildcard include/config/INLINE_SPIN_TRYLOCK) \
    $(wildcard include/config/INLINE_SPIN_TRYLOCK_BH) \
    $(wildcard include/config/UNINLINE_SPIN_UNLOCK) \
    $(wildcard include/config/INLINE_SPIN_UNLOCK_BH) \
    $(wildcard include/config/INLINE_SPIN_UNLOCK_IRQ) \
    $(wildcard include/config/INLINE_SPIN_UNLOCK_IRQRESTORE) \
    $(wildcard include/config/GENERIC_LOCKBREAK) \
  /root/linux-src/include/linux/rwlock_api_smp.h \
    $(wildcard include/config/INLINE_READ_LOCK) \
    $(wildcard include/config/INLINE_WRITE_LOCK) \
    $(wildcard include/config/INLINE_READ_LOCK_BH) \
    $(wildcard include/config/INLINE_WRITE_LOCK_BH) \
    $(wildcard include/config/INLINE_READ_LOCK_IRQ) \
    $(wildcard include/config/INLINE_WRITE_LOCK_IRQ) \
    $(wildcard include/config/INLINE_READ_LOCK_IRQSAVE) \
    $(wildcard include/config/INLINE_WRITE_LOCK_IRQSAVE) \
    $(wildcard include/config/INLINE_READ_TRYLOCK) \
    $(wildcard include/config/INLINE_WRITE_TRYLOCK) \
    $(wildcard include/config/INLINE_READ_UNLOCK) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK) \
    $(wildcard include/config/INLINE_READ_UNLOCK_BH) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK_BH) \
    $(wildcard include/config/INLINE_READ_UNLOCK_IRQ) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK_IRQ) \
    $(wildcard include/config/INLINE_READ_UNLOCK_IRQRESTORE) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK_IRQRESTORE) \
  /root/linux-src/include/uapi/linux/wait.h \
  /root/linux-src/include/linux/kdev_t.h \
  /root/linux-src/include/uapi/linux/kdev_t.h \
  /root/linux-src/include/linux/dcache.h \
  /root/linux-src/include/linux/rculist.h \
    $(wildcard include/config/PROVE_RCU_LIST) \
  /root/linux-src/include/linux/rcupdate.h \
    $(wildcard include/config/PREEMPT_RCU) \
    $(wildcard include/config/TINY_RCU) \
    $(wildcard include/config/RCU_STRICT_GRACE_PERIOD) \
    $(wildcard include/config/TASKS_RCU_GENERIC) \
    $(wildcard include/config/RCU_STALL_COMMON) \
    $(wildcard include/config/NO_HZ_FULL) \
    $(wildcard include/config/KVM_XFER_TO_GUEST_WORK) \
    $(wildcard include/config/RCU_NOCB_CPU) \
    $(wildcard include/config/TASKS_RCU) \
    $(wildcard include/config/TASKS_TRACE_RCU) \
    $(wildcard include/config/TASKS_RUDE_RCU) \
    $(wildcard include/config/TREE_RCU) \
    $(wildcard include/config/DEBUG_OBJECTS_RCU_HEAD) \
    $(wildcard include/config/PROVE_RCU) \
    $(wildcard include/config/ARCH_WEAK_RELEASE_ACQUIRE) \
  /root/linux-src/include/linux/context_tracking_irq.h \
    $(wildcard include/config/CONTEXT_TRACKING_IDLE) \
  /root/linux-src/include/linux/rcutree.h \
  /root/linux-src/include/linux/rculist_bl.h \
  /root/linux-src/include/linux/list_bl.h \
  /root/linux-src/include/linux/bit_spinlock.h \
  /root/linux-src/include/linux/seqlock.h \
  /root/linux-src/include/linux/mutex.h \
    $(wildcard include/config/MUTEX_SPIN_ON_OWNER) \
    $(wildcard include/config/DEBUG_MUTEXES) \
  /root/linux-src/include/linux/osq_lock.h \
  /root/linux-src/include/linux/debug_locks.h \
  /root/linux-src/include/linux/lockref.h \
    $(wildcard include/config/ARCH_USE_CMPXCHG_LOCKREF) \
  include/generated/bounds.h \
  /root/linux-src/include/linux/stringhash.h \
    $(wildcard include/config/DCACHE_WORD_ACCESS) \
  /root/linux-src/include/linux/hash.h \
    $(wildcard include/config/HAVE_ARCH_HASH) \
  /root/linux-src/include/linux/path.h \
  /root/linux-src/include/linux/stat.h \
  /root/linux-src/arch/x86/include/uapi/asm/stat.h \
  /root/linux-src/include/uapi/linux/stat.h \
  /root/linux-src/include/linux/time.h \
    $(wildcard include/config/POSIX_TIMERS) \
  /root/linux-src/include/linux/time32.h \
  /root/linux-src/include/linux/timex.h \
  /root/linux-src/include/uapi/linux/timex.h \
  /root/linux-src/arch/x86/include/asm/timex.h \
    $(wildcard include/config/X86_TSC) \
  /root/linux-src/arch/x86/include/asm/tsc.h \
  /root/linux-src/include/vdso/time32.h \
  /root/linux-src/include/vdso/time.h \
  /root/linux-src/include/linux/uidgid.h \
    $(wildcard include/config/MULTIUSER) \
    $(wildcard include/config/USER_NS) \
  /root/linux-src/include/linux/highuid.h \
  /root/linux-src/include/linux/list_lru.h \
    $(wildcard include/config/MEMCG_KMEM) \
  /root/linux-src/include/linux/nodemask.h \
    $(wildcard include/config/HIGHMEM) \
  /root/linux-src/include/linux/random.h \
    $(wildcard include/config/VMGENID) \
  /root/linux-src/include/linux/once.h \
  /root/linux-src/include/uapi/linux/random.h \
  /root/linux-src/include/linux/irqnr.h \
  /root/linux-src/include/uapi/linux/irqnr.h \
  /root/linux-src/include/linux/prandom.h \
  /root/linux-src/include/linux/percpu.h \
    $(wildcard include/config/NEED_PER_CPU_EMBED_FIRST_CHUNK) \
    $(wildcard include/config/NEED_PER_CPU_PAGE_FIRST_CHUNK) \
  /root/linux-src/include/linux/mmdebug.h \
    $(wildcard include/config/DEBUG_VM) \
    $(wildcard include/config/DEBUG_VM_IRQSOFF) \
    $(wildcard include/config/DEBUG_VM_PGFLAGS) \
  /root/linux-src/arch/x86/include/asm/archrandom.h \
    $(wildcard include/config/UML) \
  /root/linux-src/include/linux/shrinker.h \
    $(wildcard include/config/MEMCG) \
    $(wildcard include/config/SHRINKER_DEBUG) \
  /root/linux-src/include/linux/xarray.h \
    $(wildcard include/config/XARRAY_MULTI) \
  /root/linux-src/include/linux/gfp.h \
    $(wildcard include/config/ZONE_DMA) \
    $(wildcard include/config/ZONE_DMA32) \
    $(wildcard include/config/ZONE_DEVICE) \
    $(wildcard include/config/PM_SLEEP) \
    $(wildcard include/config/CONTIG_ALLOC) \
    $(wildcard include/config/CMA) \
  /root/linux-src/include/linux/mmzone.h \
    $(wildcard include/config/ARCH_FORCE_MAX_ORDER) \
    $(wildcard include/config/MEMORY_ISOLATION) \
    $(wildcard include/config/ZSMALLOC) \
    $(wildcard include/config/SWAP) \
    $(wildcard include/config/NUMA_BALANCING) \
    $(wildcard include/config/TRANSPARENT_HUGEPAGE) \
    $(wildcard include/config/LRU_GEN) \
    $(wildcard include/config/LRU_GEN_STATS) \
    $(wildcard include/config/MEMORY_HOTPLUG) \
    $(wildcard include/config/COMPACTION) \
    $(wildcard include/config/PAGE_EXTENSION) \
    $(wildcard include/config/DEFERRED_STRUCT_PAGE_INIT) \
    $(wildcard include/config/HAVE_MEMORYLESS_NODES) \
    $(wildcard include/config/SPARSEMEM_EXTREME) \
    $(wildcard include/config/HAVE_ARCH_PFN_VALID) \
  /root/linux-src/include/linux/pageblock-flags.h \
    $(wildcard include/config/HUGETLB_PAGE) \
    $(wildcard include/config/HUGETLB_PAGE_SIZE_VARIABLE) \
  /root/linux-src/include/linux/page-flags-layout.h \
  /root/linux-src/include/linux/mm_types.h \
    $(wildcard include/config/HAVE_ALIGNED_STRUCT_PAGE) \
    $(wildcard include/config/USERFAULTFD) \
    $(wildcard include/config/HAVE_ARCH_COMPAT_MMAP_BASES) \
    $(wildcard include/config/MEMBARRIER) \
    $(wildcard include/config/AIO) \
    $(wildcard include/config/MMU_NOTIFIER) \
    $(wildcard include/config/ARCH_WANT_BATCHED_UNMAP_TLB_FLUSH) \
    $(wildcard include/config/IOMMU_SVA) \
    $(wildcard include/config/KSM) \
  /root/linux-src/include/linux/mm_types_task.h \
    $(wildcard include/config/SPLIT_PTLOCK_CPUS) \
    $(wildcard include/config/ARCH_ENABLE_SPLIT_PMD_PTLOCK) \
  /root/linux-src/arch/x86/include/asm/tlbbatch.h \
  /root/linux-src/include/linux/auxvec.h \
  /root/linux-src/include/uapi/linux/auxvec.h \
  /root/linux-src/arch/x86/include/uapi/asm/auxvec.h \
  /root/linux-src/include/linux/kref.h \
  /root/linux-src/include/linux/refcount.h \
  /root/linux-src/include/linux/rbtree.h \
  /root/linux-src/include/linux/rbtree_types.h \
  /root/linux-src/include/linux/maple_tree.h \
    $(wildcard include/config/MAPLE_RCU_DISABLED) \
    $(wildcard include/config/DEBUG_MAPLE_TREE_VERBOSE) \
    $(wildcard include/config/DEBUG_MAPLE_TREE) \
  /root/linux-src/include/linux/rwsem.h \
    $(wildcard include/config/RWSEM_SPIN_ON_OWNER) \
    $(wildcard include/config/DEBUG_RWSEMS) \
  /root/linux-src/include/linux/completion.h \
  /root/linux-src/include/linux/swait.h \
  /root/linux-src/include/linux/uprobes.h \
    $(wildcard include/config/UPROBES) \
  /root/linux-src/arch/x86/include/asm/uprobes.h \
  /root/linux-src/include/linux/notifier.h \
    $(wildcard include/config/TREE_SRCU) \
  /root/linux-src/include/linux/srcu.h \
    $(wildcard include/config/TINY_SRCU) \
    $(wildcard include/config/SRCU) \
  /root/linux-src/include/linux/workqueue.h \
    $(wildcard include/config/DEBUG_OBJECTS_WORK) \
    $(wildcard include/config/FREEZER) \
    $(wildcard include/config/SYSFS) \
    $(wildcard include/config/WQ_WATCHDOG) \
  /root/linux-src/include/linux/timer.h \
    $(wildcard include/config/DEBUG_OBJECTS_TIMERS) \
  /root/linux-src/include/linux/ktime.h \
  /root/linux-src/include/linux/jiffies.h \
  /root/linux-src/include/vdso/jiffies.h \
  include/generated/timeconst.h \
  /root/linux-src/include/vdso/ktime.h \
  /root/linux-src/include/linux/timekeeping.h \
    $(wildcard include/config/GENERIC_CMOS_UPDATE) \
  /root/linux-src/include/linux/clocksource_ids.h \
  /root/linux-src/include/linux/debugobjects.h \
    $(wildcard include/config/DEBUG_OBJECTS) \
    $(wildcard include/config/DEBUG_OBJECTS_FREE) \
  /root/linux-src/include/linux/rcu_segcblist.h \
  /root/linux-src/include/linux/srcutree.h \
  /root/linux-src/include/linux/rcu_node_tree.h \
    $(wildcard include/config/RCU_FANOUT) \
    $(wildcard include/config/RCU_FANOUT_LEAF) \
  /root/linux-src/arch/x86/include/asm/mmu.h \
    $(wildcard include/config/MODIFY_LDT_SYSCALL) \
  /root/linux-src/include/linux/page-flags.h \
    $(wildcard include/config/ARCH_USES_PG_UNCACHED) \
    $(wildcard include/config/MEMORY_FAILURE) \
    $(wildcard include/config/PAGE_IDLE_FLAG) \
    $(wildcard include/config/HUGETLB_PAGE_OPTIMIZE_VMEMMAP) \
  /root/linux-src/include/linux/local_lock.h \
  /root/linux-src/include/linux/local_lock_internal.h \
  /root/linux-src/include/linux/memory_hotplug.h \
    $(wildcard include/config/HAVE_ARCH_NODEDATA_EXTENSION) \
    $(wildcard include/config/ARCH_HAS_ADD_PAGES) \
    $(wildcard include/config/MEMORY_HOTREMOVE) \
  /root/linux-src/arch/x86/include/asm/mmzone.h \
  /root/linux-src/arch/x86/include/asm/mmzone_64.h \
  /root/linux-src/include/linux/topology.h \
    $(wildcard include/config/USE_PERCPU_NUMA_NODE_ID) \
    $(wildcard include/config/SCHED_SMT) \
  /root/linux-src/include/linux/arch_topology.h \
    $(wildcard include/config/ACPI_CPPC_LIB) \
    $(wildcard include/config/GENERIC_ARCH_TOPOLOGY) \
  /root/linux-src/arch/x86/include/asm/topology.h \
    $(wildcard include/config/SCHED_MC_PRIO) \
  /root/linux-src/arch/x86/include/asm/mpspec.h \
    $(wildcard include/config/EISA) \
    $(wildcard include/config/X86_MPPARSE) \
  /root/linux-src/arch/x86/include/asm/mpspec_def.h \
  /root/linux-src/arch/x86/include/asm/x86_init.h \
  /root/linux-src/arch/x86/include/asm/apicdef.h \
  /root/linux-src/include/asm-generic/topology.h \
  /root/linux-src/include/linux/kconfig.h \
  /root/linux-src/include/linux/sched/mm.h \
    $(wildcard include/config/ARCH_HAS_MEMBARRIER_CALLBACKS) \
  /root/linux-src/include/linux/sched.h \
    $(wildcard include/config/VIRT_CPU_ACCOUNTING_NATIVE) \
    $(wildcard include/config/SCHED_INFO) \
    $(wildcard include/config/SCHEDSTATS) \
    $(wildcard include/config/SCHED_CORE) \
    $(wildcard include/config/FAIR_GROUP_SCHED) \
    $(wildcard include/config/RT_GROUP_SCHED) \
    $(wildcard include/config/RT_MUTEXES) \
    $(wildcard include/config/UCLAMP_TASK) \
    $(wildcard include/config/UCLAMP_BUCKETS_COUNT) \
    $(wildcard include/config/KMAP_LOCAL) \
    $(wildcard include/config/CGROUP_SCHED) \
    $(wildcard include/config/BLK_DEV_IO_TRACE) \
    $(wildcard include/config/PSI) \
    $(wildcard include/config/COMPAT_BRK) \
    $(wildcard include/config/CGROUPS) \
    $(wildcard include/config/BLK_CGROUP) \
    $(wildcard include/config/PAGE_OWNER) \
    $(wildcard include/config/EVENTFD) \
    $(wildcard include/config/CPU_SUP_INTEL) \
    $(wildcard include/config/TASK_DELAY_ACCT) \
    $(wildcard include/config/ARCH_HAS_SCALED_CPUTIME) \
    $(wildcard include/config/VIRT_CPU_ACCOUNTING_GEN) \
    $(wildcard include/config/POSIX_CPUTIMERS) \
    $(wildcard include/config/POSIX_CPU_TIMERS_TASK_WORK) \
    $(wildcard include/config/KEYS) \
    $(wildcard include/config/SYSVIPC) \
    $(wildcard include/config/DETECT_HUNG_TASK) \
    $(wildcard include/config/IO_URING) \
    $(wildcard include/config/AUDIT) \
    $(wildcard include/config/AUDITSYSCALL) \
    $(wildcard include/config/UBSAN) \
    $(wildcard include/config/UBSAN_TRAP) \
    $(wildcard include/config/TASK_XACCT) \
    $(wildcard include/config/CPUSETS) \
    $(wildcard include/config/X86_CPU_RESCTRL) \
    $(wildcard include/config/FUTEX) \
    $(wildcard include/config/PERF_EVENTS) \
    $(wildcard include/config/RSEQ) \
    $(wildcard include/config/FAULT_INJECTION) \
    $(wildcard include/config/LATENCYTOP) \
    $(wildcard include/config/KUNIT) \
    $(wildcard include/config/FUNCTION_GRAPH_TRACER) \
    $(wildcard include/config/BCACHE) \
    $(wildcard include/config/VMAP_STACK) \
    $(wildcard include/config/LIVEPATCH) \
    $(wildcard include/config/BPF_SYSCALL) \
    $(wildcard include/config/GCC_PLUGIN_STACKLEAK) \
    $(wildcard include/config/X86_MCE) \
    $(wildcard include/config/KRETPROBES) \
    $(wildcard include/config/RETHOOK) \
    $(wildcard include/config/ARCH_HAS_PARANOID_L1D_FLUSH) \
    $(wildcard include/config/RV) \
    $(wildcard include/config/ARCH_TASK_STRUCT_ON_STACK) \
    $(wildcard include/config/PREEMPT_NONE) \
    $(wildcard include/config/PREEMPT_VOLUNTARY) \
    $(wildcard include/config/DEBUG_RSEQ) \
  /root/linux-src/include/uapi/linux/sched.h \
  /root/linux-src/include/linux/pid.h \
  /root/linux-src/include/linux/sem.h \
  /root/linux-src/include/uapi/linux/sem.h \
  /root/linux-src/include/linux/ipc.h \
  /root/linux-src/include/linux/rhashtable-types.h \
  /root/linux-src/include/uapi/linux/ipc.h \
  arch/x86/include/generated/uapi/asm/ipcbuf.h \
  /root/linux-src/include/uapi/asm-generic/ipcbuf.h \
  /root/linux-src/arch/x86/include/uapi/asm/sembuf.h \
  /root/linux-src/include/linux/shm.h \
  /root/linux-src/include/uapi/linux/shm.h \
  /root/linux-src/include/uapi/asm-generic/hugetlb_encode.h \
  /root/linux-src/arch/x86/include/uapi/asm/shmbuf.h \
  /root/linux-src/include/uapi/asm-generic/shmbuf.h \
  /root/linux-src/arch/x86/include/asm/shmparam.h \
  /root/linux-src/include/linux/kmsan_types.h \
  /root/linux-src/include/linux/plist.h \
    $(wildcard include/config/DEBUG_PLIST) \
  /root/linux-src/include/linux/hrtimer.h \
    $(wildcard include/config/HIGH_RES_TIMERS) \
    $(wildcard include/config/TIME_LOW_RES) \
    $(wildcard include/config/TIMERFD) \
  /root/linux-src/include/linux/hrtimer_defs.h \
  /root/linux-src/include/linux/timerqueue.h \
  /root/linux-src/include/linux/seccomp.h \
    $(wildcard include/config/SECCOMP) \
    $(wildcard include/config/HAVE_ARCH_SECCOMP_FILTER) \
    $(wildcard include/config/SECCOMP_FILTER) \
    $(wildcard include/config/CHECKPOINT_RESTORE) \
    $(wildcard include/config/SECCOMP_CACHE_DEBUG) \
  /root/linux-src/include/uapi/linux/seccomp.h \
  /root/linux-src/arch/x86/include/asm/seccomp.h \
  /root/linux-src/arch/x86/include/asm/unistd.h \
  /root/linux-src/arch/x86/include/uapi/asm/unistd.h \
  arch/x86/include/generated/uapi/asm/unistd_64.h \
  arch/x86/include/generated/asm/unistd_64_x32.h \
  arch/x86/include/generated/asm/unistd_32_ia32.h \
  /root/linux-src/arch/x86/include/asm/ia32_unistd.h \
  /root/linux-src/include/asm-generic/seccomp.h \
  /root/linux-src/include/uapi/linux/unistd.h \
  /root/linux-src/include/linux/resource.h \
  /root/linux-src/include/uapi/linux/resource.h \
  arch/x86/include/generated/uapi/asm/resource.h \
  /root/linux-src/include/asm-generic/resource.h \
  /root/linux-src/include/uapi/asm-generic/resource.h \
  /root/linux-src/include/linux/latencytop.h \
  /root/linux-src/include/linux/sched/prio.h \
  /root/linux-src/include/linux/sched/types.h \
  /root/linux-src/include/linux/signal_types.h \
    $(wildcard include/config/OLD_SIGACTION) \
  /root/linux-src/include/uapi/linux/signal.h \
  /root/linux-src/arch/x86/include/asm/signal.h \
  /root/linux-src/arch/x86/include/uapi/asm/signal.h \
  /root/linux-src/include/uapi/asm-generic/signal-defs.h \
  /root/linux-src/arch/x86/include/uapi/asm/siginfo.h \
  /root/linux-src/include/uapi/asm-generic/siginfo.h \
  /root/linux-src/include/linux/syscall_user_dispatch.h \
  /root/linux-src/include/linux/task_io_accounting.h \
    $(wildcard include/config/TASK_IO_ACCOUNTING) \
  /root/linux-src/include/linux/posix-timers.h \
  /root/linux-src/include/linux/alarmtimer.h \
    $(wildcard include/config/RTC_CLASS) \
  /root/linux-src/include/uapi/linux/rseq.h \
  /root/linux-src/include/linux/kcsan.h \
  /root/linux-src/include/linux/rv.h \
    $(wildcard include/config/RV_REACTORS) \
  arch/x86/include/generated/asm/kmap_size.h \
  /root/linux-src/include/asm-generic/kmap_size.h \
    $(wildcard include/config/DEBUG_KMAP_LOCAL) \
  /root/linux-src/include/linux/sync_core.h \
    $(wildcard include/config/ARCH_HAS_SYNC_CORE_BEFORE_USERMODE) \
  /root/linux-src/arch/x86/include/asm/sync_core.h \
  /root/linux-src/include/linux/ioasid.h \
    $(wildcard include/config/IOASID) \
  /root/linux-src/include/linux/radix-tree.h \
  /root/linux-src/include/linux/capability.h \
  /root/linux-src/include/uapi/linux/capability.h \
  /root/linux-src/include/linux/semaphore.h \
  /root/linux-src/include/linux/fcntl.h \
    $(wildcard include/config/ARCH_32BIT_OFF_T) \
  /root/linux-src/include/uapi/linux/fcntl.h \
  arch/x86/include/generated/uapi/asm/fcntl.h \
  /root/linux-src/include/uapi/asm-generic/fcntl.h \
  /root/linux-src/include/uapi/linux/openat2.h \
  /root/linux-src/include/linux/migrate_mode.h \
  /root/linux-src/include/linux/percpu-rwsem.h \
  /root/linux-src/include/linux/rcuwait.h \
  /root/linux-src/include/linux/sched/signal.h \
    $(wildcard include/config/SCHED_AUTOGROUP) \
    $(wildcard include/config/BSD_PROCESS_ACCT) \
    $(wildcard include/config/TASKSTATS) \
    $(wildcard include/config/STACK_GROWSUP) \
  /root/linux-src/include/linux/signal.h \
    $(wildcard include/config/DYNAMIC_SIGFRAME) \
  /root/linux-src/include/linux/sched/jobctl.h \
  /root/linux-src/include/linux/sched/task.h \
    $(wildcard include/config/HAVE_EXIT_THREAD) \
    $(wildcard include/config/ARCH_WANTS_DYNAMIC_TASK_STRUCT) \
    $(wildcard include/config/HAVE_ARCH_THREAD_STRUCT_WHITELIST) \
  /root/linux-src/include/linux/uaccess.h \
    $(wildcard include/config/ARCH_HAS_SUBPAGE_FAULTS) \
  /root/linux-src/include/linux/fault-inject-usercopy.h \
    $(wildcard include/config/FAULT_INJECTION_USERCOPY) \
  /root/linux-src/arch/x86/include/asm/uaccess.h \
    $(wildcard include/config/CC_HAS_ASM_GOTO_OUTPUT) \
    $(wildcard include/config/CC_HAS_ASM_GOTO_TIED_OUTPUT) \
    $(wildcard include/config/ARCH_HAS_COPY_MC) \
    $(wildcard include/config/X86_INTEL_USERCOPY) \
  /root/linux-src/arch/x86/include/asm/smap.h \
  /root/linux-src/arch/x86/include/asm/extable.h \
    $(wildcard include/config/BPF_JIT) \
  /root/linux-src/include/asm-generic/access_ok.h \
    $(wildcard include/config/ALTERNATE_USER_ADDRESS_SPACE) \
  /root/linux-src/arch/x86/include/asm/uaccess_64.h \
  /root/linux-src/include/linux/cred.h \
    $(wildcard include/config/DEBUG_CREDENTIALS) \
  /root/linux-src/include/linux/key.h \
    $(wildcard include/config/KEY_NOTIFICATIONS) \
    $(wildcard include/config/NET) \
    $(wildcard include/config/SYSCTL) \
  /root/linux-src/include/linux/sysctl.h \
  /root/linux-src/include/uapi/linux/sysctl.h \
  /root/linux-src/include/linux/assoc_array.h \
    $(wildcard include/config/ASSOCIATIVE_ARRAY) \
  /root/linux-src/include/linux/sched/user.h \
    $(wildcard include/config/VFIO_PCI_ZDEV_KVM) \
    $(wildcard include/config/WATCH_QUEUE) \
  /root/linux-src/include/linux/percpu_counter.h \
  /root/linux-src/include/linux/ratelimit.h \
  /root/linux-src/include/linux/rcu_sync.h \
  /root/linux-src/include/linux/delayed_call.h \
  /root/linux-src/include/linux/uuid.h \
  /root/linux-src/include/uapi/linux/uuid.h \
  /root/linux-src/include/linux/errseq.h \
  /root/linux-src/include/linux/ioprio.h \
  /root/linux-src/include/linux/sched/rt.h \
  /root/linux-src/include/linux/iocontext.h \
    $(wildcard include/config/BLK_ICQ) \
  /root/linux-src/include/uapi/linux/ioprio.h \
  /root/linux-src/include/linux/fs_types.h \
  /root/linux-src/include/linux/mount.h \
  /root/linux-src/include/linux/mnt_idmapping.h \
  /root/linux-src/include/linux/slab.h \
    $(wildcard include/config/DEBUG_SLAB) \
    $(wildcard include/config/FAILSLAB) \
    $(wildcard include/config/KFENCE) \
    $(wildcard include/config/SLAB) \
    $(wildcard include/config/SLUB) \
    $(wildcard include/config/SLOB) \
  /root/linux-src/include/linux/overflow.h \
  /root/linux-src/include/linux/percpu-refcount.h \
  /root/linux-src/include/linux/kasan.h \
    $(wildcard include/config/KASAN_STACK) \
    $(wildcard include/config/KASAN_VMALLOC) \
    $(wildcard include/config/KASAN_INLINE) \
  /root/linux-src/include/linux/kasan-enabled.h \
  /root/linux-src/include/uapi/linux/fs.h \
  /root/linux-src/include/linux/quota.h \
    $(wildcard include/config/QUOTA_NETLINK_INTERFACE) \
  /root/linux-src/include/uapi/linux/dqblk_xfs.h \
  /root/linux-src/include/linux/dqblk_v1.h \
  /root/linux-src/include/linux/dqblk_v2.h \
  /root/linux-src/include/linux/dqblk_qtree.h \
  /root/linux-src/include/linux/projid.h \
  /root/linux-src/include/uapi/linux/quota.h \
  /root/linux-src/include/linux/nfs_fs_i.h \
  /root/linux-src/include/linux/miscdevice.h \
  /root/linux-src/include/uapi/linux/major.h \
  /root/linux-src/include/linux/device.h \
    $(wildcard include/config/GENERIC_MSI_IRQ_DOMAIN) \
    $(wildcard include/config/GENERIC_MSI_IRQ) \
    $(wildcard include/config/ENERGY_MODEL) \
    $(wildcard include/config/PINCTRL) \
    $(wildcard include/config/DMA_OPS) \
    $(wildcard include/config/DMA_DECLARE_COHERENT) \
    $(wildcard include/config/DMA_CMA) \
    $(wildcard include/config/SWIOTLB) \
    $(wildcard include/config/ARCH_HAS_SYNC_DMA_FOR_DEVICE) \
    $(wildcard include/config/ARCH_HAS_SYNC_DMA_FOR_CPU) \
    $(wildcard include/config/ARCH_HAS_SYNC_DMA_FOR_CPU_ALL) \
    $(wildcard include/config/DMA_OPS_BYPASS) \
    $(wildcard include/config/OF) \
    $(wildcard include/config/DEVTMPFS) \
    $(wildcard include/config/SYSFS_DEPRECATED) \
  /root/linux-src/include/linux/dev_printk.h \
  /root/linux-src/include/linux/energy_model.h \
  /root/linux-src/include/linux/kobject.h \
    $(wildcard include/config/UEVENT_HELPER) \
    $(wildcard include/config/DEBUG_KOBJECT_RELEASE) \
  /root/linux-src/include/linux/sysfs.h \
  /root/linux-src/include/linux/kernfs.h \
    $(wildcard include/config/KERNFS) \
  /root/linux-src/include/linux/idr.h \
  /root/linux-src/include/linux/kobject_ns.h \
  /root/linux-src/include/linux/sched/cpufreq.h \
    $(wildcard include/config/CPU_FREQ) \
  /root/linux-src/include/linux/sched/topology.h \
    $(wildcard include/config/SCHED_DEBUG) \
    $(wildcard include/config/SCHED_CLUSTER) \
    $(wildcard include/config/SCHED_MC) \
    $(wildcard include/config/CPU_FREQ_GOV_SCHEDUTIL) \
  /root/linux-src/include/linux/sched/idle.h \
  /root/linux-src/include/linux/sched/sd_flags.h \
  /root/linux-src/include/linux/ioport.h \
  /root/linux-src/include/linux/klist.h \
  /root/linux-src/include/linux/pm.h \
    $(wildcard include/config/VT_CONSOLE_SLEEP) \
    $(wildcard include/config/CXL_SUSPEND) \
    $(wildcard include/config/PM) \
    $(wildcard include/config/PM_CLK) \
    $(wildcard include/config/PM_GENERIC_DOMAINS) \
  /root/linux-src/include/linux/device/bus.h \
    $(wildcard include/config/ACPI) \
  /root/linux-src/include/linux/device/class.h \
  /root/linux-src/include/linux/device/driver.h \
  /root/linux-src/include/linux/module.h \
    $(wildcard include/config/MODULES_TREE_LOOKUP) \
    $(wildcard include/config/STACKTRACE_BUILD_ID) \
    $(wildcard include/config/ARCH_USES_CFI_TRAPS) \
    $(wildcard include/config/MODULE_SIG) \
    $(wildcard include/config/ARCH_WANTS_MODULES_DATA_IN_VMALLOC) \
    $(wildcard include/config/KALLSYMS) \
    $(wildcard include/config/BPF_EVENTS) \
    $(wildcard include/config/DEBUG_INFO_BTF_MODULES) \
    $(wildcard include/config/EVENT_TRACING) \
    $(wildcard include/config/MODULE_UNLOAD) \
    $(wildcard include/config/CONSTRUCTORS) \
    $(wildcard include/config/FUNCTION_ERROR_INJECTION) \
  /root/linux-src/include/linux/buildid.h \
    $(wildcard include/config/CRASH_CORE) \
  /root/linux-src/include/linux/kmod.h \
  /root/linux-src/include/linux/umh.h \
  /root/linux-src/include/linux/elf.h \
    $(wildcard include/config/ARCH_USE_GNU_PROPERTY) \
    $(wildcard include/config/ARCH_HAVE_ELF_PROT) \
  /root/linux-src/arch/x86/include/asm/elf.h \
    $(wildcard include/config/X86_X32_ABI) \
  /root/linux-src/arch/x86/include/asm/user.h \
  /root/linux-src/arch/x86/include/asm/user_64.h \
  /root/linux-src/arch/x86/include/asm/fsgsbase.h \
  /root/linux-src/arch/x86/include/asm/vdso.h \
  /root/linux-src/include/uapi/linux/elf.h \
  /root/linux-src/include/uapi/linux/elf-em.h \
  /root/linux-src/include/linux/moduleparam.h \
    $(wildcard include/config/ALPHA) \
    $(wildcard include/config/IA64) \
    $(wildcard include/config/PPC64) \
  /root/linux-src/include/linux/rbtree_latch.h \
  /root/linux-src/include/linux/error-injection.h \
  /root/linux-src/include/asm-generic/error-injection.h \
  /root/linux-src/arch/x86/include/asm/module.h \
    $(wildcard include/config/UNWINDER_ORC) \
  /root/linux-src/include/asm-generic/module.h \
    $(wildcard include/config/HAVE_MOD_ARCH_SPECIFIC) \
    $(wildcard include/config/MODULES_USE_ELF_REL) \
    $(wildcard include/config/MODULES_USE_ELF_RELA) \
  /root/linux-src/arch/x86/include/asm/orc_types.h \
  /root/linux-src/arch/x86/include/asm/device.h \
  /root/linux-src/include/linux/pm_wakeup.h \
  /mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/../include/uapi/hwrun.h \

/mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/hwrun_core.o: $(deps_/mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/hwrun_core.o)

$(deps_/mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/hwrun_core.o):

/mnt/c/Users/xingg/Desktop/hwrun-os-linux/kernel/modules/hwrun_core.o: $(wildcard ./tools/objtool/objtool)
