/*
 * loader.h — HWRun OS 可执行文件加载协议（LOADER）公共接口
 *
 * 本头文件定义 LOADER 插件对外暴露的数据结构与接口。
 * 上层插件（SHELL/RUN/PKGMGR/INIT/...）通过 get_interface("LOADER")
 * 获取 hw_loader_ops_t，从而统一检测 / 加载 / 运行各种格式的可执行文件，
 * 不直接操作 Linux binfmt。
 *
 * 设计原则：协议驱动、统一加载入口、按魔数自动识别格式、机制而非策略。
 * 在 MSYS2/Windows 等降级 / 自测环境下，读取失败一律返回负 errno 而非崩溃。
 */

#ifndef HW_LOADER_H
#define HW_LOADER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 可执行文件格式类型
 * ============================================================ */
typedef enum {
    LOADER_FORMAT_UNKNOWN = 0,
    LOADER_FORMAT_ELF,      /* ELF  可执行 / 共享库 */
    LOADER_FORMAT_PE,       /* PE/COFF (Windows)    */
    LOADER_FORMAT_MACHO,    /* Mach-O (macOS)       */
    LOADER_FORMAT_APK,      /* APK / ZIP            */
    LOADER_FORMAT_WASM,     /* WebAssembly          */
    LOADER_FORMAT_SCRIPT,   /* shebang 脚本          */
    LOADER_FORMAT_JAVA,     /* CLASS 字节码 (预留)   */
} loader_format_t;

/* ============================================================
 * 格式信息（用于 get_formats）
 * ============================================================ */
typedef struct loader_format_info {
    loader_format_t format;
    char name[32];                  /* "ELF" / "PE/COFF" / ... */
    char magic[24];                 /* 魔数（十六进制可读串）  */
    uint32_t magic_len;             /* 魔数字节数             */
    char extension[16];             /* 文件扩展名提示          */
    int  supported;                 /* 是否支持加载           */
} loader_format_info_t;

/* ============================================================
 * ELF 程序头概要
 * ============================================================ */
typedef struct loader_ph_info {
    uint32_t p_type;                /* PT_LOAD/PT_INTERP/...    */
    uint32_t p_flags;               /* PF_R/PF_W/PF_X            */
    uint64_t p_offset;              /* 文件内偏移                */
    uint64_t p_vaddr;               /* 虚拟地址                 */
    uint64_t p_paddr;               /* 物理地址(与虚拟通常相同) */
    uint64_t p_filesz;              /* 文件内大小               */
    uint64_t p_memsz;               /* 内存中大小               */
} loader_ph_info_t;

/* ============================================================
 * ELF 头解析结果
 * ============================================================ */
typedef struct loader_elf_info {
    uint8_t  ei_class;              /* ELFCLASS32 / ELFCLASS64   */
    uint8_t  ei_data;               /* ELFDATA2LSB / ELFDATA2MSB */
    uint16_t e_type;                /* ET_REL/EXEC/DYN/CORE       */
    uint16_t e_machine;             /* EM_X86_64 / EM_AARCH64 ... */
    uint32_t e_flags;
    uint64_t e_entry;               /* 入口点地址                */
    uint16_t e_shstrndx;            /* 节名字符串表索引          */
    uint32_t e_phnum;               /* 程序头个数                */
    uint32_t e_shnum;               /* 节头个数                  */
    int      has_interp;            /* 是否含 PT_INTERP(解释器)  */
    uint32_t ph_count;              /* 已提取的程序头概要个数    */
    loader_ph_info_t ph[8];         /* 程序头概要（至多 8 个）   */
    char     section_names[512];    /* 逗号分隔的节名概要        */
} loader_elf_info_t;

/* ============================================================
 * 加载句柄（表示一个已加载/驻留的程序或库）
 * ============================================================ */
enum {
    LOADER_MODE_NONE    = 0,
    LOADER_MODE_INFO,   /* 仅解析信息（可执行文件，本机架构 exec） */
    LOADER_MODE_DLOPEN, /* 已用 dlopen 驻留的共享库              */
};

typedef struct loader_handle {
    uint64_t     id;
    loader_format_t format;
    char         path[256];
    char         interpreter[256];   /* 脚本解释器                */
    void        *dl;                 /* dlopen 句柄（共享库）     */
    int          mode;               /* LOADER_MODE_*            */
    int32_t      pid;                /* 派生的子进程 PID（无则 0） */
    uint64_t     loaded_at;          /* 加载时间戳                */
} loader_handle_t;

/* ============================================================
 * LOADER 对外协议接口（get_interface("LOADER") 返回此结构）
 * ============================================================ */
typedef struct hw_loader_ops {
    /* 检测文件格式：读取魔数 → 填入 format/name，脚本解析其解释器 */
    int (*detect)(const char *path, loader_format_t *fmt,
                  char *format_name, size_t name_size,
                  char *interpreter, size_t interp_size);

    /* 解析 ELF 头：填充 class/type/machine/entry + 程序头/节名概要 */
    int (*elf_parse)(const char *path, loader_elf_info_t *out);

    /* 加载（不运行）：对可执行/脚本仅解析描述；对 ELF 共享库用 dlopen
     * 真实驻留到 handle->dl。返回 HWRUN_OK 或负 errno。 */
    int (*load)(const char *path, loader_handle_t *h);

    /* 运行：对可执行/脚本用 fork+exec 真实启动（wait 则同步等待取值）；
     * 对 ELF 共享库则 dlopen 返回负 errno=HWRUN_ENOTSUP（库应用 load）。
     * 返回子进程 PID（wait=0）或退出码（wait 非 0）；失败返回负 errno。 */
    int (*run)(const char *path, char **argv, int wait, int *exit_code);

    /* 卸载：dlclose 已 dlopen 的库句柄 */
    int (*unload)(loader_handle_t *h);

    /* 列出支持的所有格式 */
    int (*get_formats)(loader_format_info_t *out, int cap);
} hw_loader_ops_t;

/* 供插件内部使用的实现函数声明（各 *impl 由 src/ 实现） */
int loader_detect_impl(const char *path, loader_format_t *fmt,
                       char *format_name, size_t name_size,
                       char *interpreter, size_t interp_size);
int loader_elf_impl(const char *path, loader_elf_info_t *out);
int loader_load_impl(const char *path, loader_handle_t *h);
int loader_run_impl(const char *path, char **argv, int wait, int *exit_code);
int loader_unload_impl(loader_handle_t *h);
int loader_formats_impl(loader_format_info_t *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* HW_LOADER_H */