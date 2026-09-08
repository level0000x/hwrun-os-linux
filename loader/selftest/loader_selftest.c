/*
 * loader_selftest.c — LOADER 插件 dlopen 自测
 *
 * 通过 dlopen 加载 build/loader.so，调用 hw_plugin_entry() 取得 hw_plugin_t*，
 * 再经 get_interface("LOADER") 取得 hw_loader_ops_t，验证：
 *   1) 插件元数据（id/provides/requires/type）
 *   2) 格式识别：本机 PE 可执行 / 合成的 ELF / Mach-O / 未知 / shebang 脚本
 *   3) ELF 头解析：对合成的 ELF 解析 class/type/machine/entry + 程序头/节名概要
 *   4) 列出支持格式
 *   5) run：脚本 shebang 解释器经 fork+exec 运行（同步等待取退出码）
 *
 * 说明：本工具链为 x86_64-pc-cygwin（产出 PE），故 ELF 实体由测试程序合成，
 *      仅用于验证"只读 ELF 头解析"，不涉及 ELF 执行。
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <dlfcn.h>

#include "hwrun.h"
#include "loader.h"

static int failures = 0;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond)                                                                                  \
            printf("  [PASS] %s\n", (msg));                                                        \
        else {                                                                                     \
            printf("  [FAIL] %s\n", (msg));                                                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ---- 合成一个最小 ELF64 头 + 程序头 + 节头 + shstrtab（仅用于解析测试）---- */
static int write_synth_elf(const char *path) {
    uint8_t b[1024];
    size_t p = 0;
    static const char shstr[] = "\0.text\0.data\0.rodata\0.bss\0.shstrtab";
    size_t shstr_len = sizeof(shstr); /* 含首尾 \0 的整个表长度 */
    uint64_t shstr_off = 0x300;       /* 与节头区 0x100..0x240 不重叠 */
    size_t nsec = 5;
    size_t i;
    FILE *f;

    memset(b, 0, sizeof(b));
    b[0] = 0x7f;
    b[1] = 'E';
    b[2] = 'L';
    b[3] = 'F';
    b[4] = 2; /* ELFCLASS64 */
    b[5] = 1; /* LSB */
    b[6] = 1;
    b[7] = 0;
    b[16] = 2;
    b[17] = 0; /* e_type = ET_EXEC */
    b[18] = 0x3e;
    b[19] = 0; /* e_machine = EM_X86_64 */
    /* e_entry (24..31) = 0x40000000 */
    b[24] = 0;
    b[25] = 0;
    b[26] = 0;
    b[27] = 0x40;
    b[28] = 0;
    b[29] = 0;
    b[30] = 0;
    b[31] = 0;
    /* e_phoff (32..39) */
    b[32] = 0x40;
    b[33] = 0;
    b[34] = 0;
    b[35] = 0;
    b[36] = 0;
    b[37] = 0;
    b[38] = 0;
    b[39] = 0;
    /* e_shoff (40..47) = 0x100 */
    b[40] = 0;
    b[41] = 1;
    b[42] = 0;
    b[43] = 0;
    b[44] = 0;
    b[45] = 0;
    b[46] = 0;
    b[47] = 0;
    b[48] = 0;
    b[49] = 0;
    b[50] = 0;
    b[51] = 0; /* e_flags */
    b[52] = 64;
    b[53] = 0; /* e_ehsize */
    b[54] = 56;
    b[55] = 0; /* e_phentsize */
    b[56] = 2; /* e_phnum = 2 */
    b[58] = 64;
    b[59] = 0;    /* e_shentsize */
    b[60] = nsec; /* e_shnum */
    b[62] = 4;    /* e_shstrndx = .shstrtab */

    /* 程序头 0: PT_LOAD @0x40 */
    b[0x40] = 1;
    b[0x41] = 0;
    b[0x42] = 0;
    b[0x43] = 0; /* p_type LOAD */
    b[0x44] = 5;
    b[0x45] = 0;
    b[0x46] = 0;
    b[0x47] = 0; /* p_flags R+X */
    /* p_offset=0x0, p_vaddr=0x400000, p_filesz=0x200, p_memsz=0x400, align=0x1000 */
    b[0x48] = 0; /* offset LSB */
    b[0x50] = 0;
    b[0x51] = 0;
    b[0x52] = 0;
    b[0x53] = 0x40;
    b[0x54] = 0;
    b[0x55] = 0;
    b[0x56] = 0;
    b[0x57] = 0; /* vaddr */
    b[0x60] = 0;
    b[0x61] = 2;
    b[0x62] = 0;
    b[0x63] = 0;
    b[0x64] = 0;
    b[0x65] = 0;
    b[0x66] = 0;
    b[0x67] = 0; /* filesz=0x200 */
    b[0x68] = 0;
    b[0x69] = 4;
    b[0x6a] = 0;
    b[0x6b] = 0;
    b[0x6c] = 0;
    b[0x6d] = 0;
    b[0x6e] = 0;
    b[0x6f] = 0; /* memsz=0x400 */
    b[0x70] = 0;
    b[0x71] = 0x10;
    b[0x72] = 0;
    b[0x73] = 0;
    b[0x74] = 0;
    b[0x75] = 0;
    b[0x76] = 0;
    b[0x77] = 0; /* align=0x1000 */

    /* 程序头 1: PT_INTERP @0x78 = 0x40 + 1*56 (ELF64 phentsize=56) */
    b[0x78] = 3;
    b[0x79] = 0;
    b[0x7a] = 0;
    b[0x7b] = 0; /* p_type INTERP */
    b[0x7c] = 4;
    b[0x7d] = 0;
    b[0x7e] = 0;
    b[0x7f] = 0; /* p_flags R    */

    /* 节头 @0x100, each 64 bytes; ELF64 shdr 布局:
         sh_name@+0, sh_type@+4, sh_flags@+8, sh_addr@+16,
         sh_offset@+24, sh_size@+32, ...                          */
    for (i = 1; i < nsec; i++) {
        uint8_t *sh = b + 0x100 + (i * 64);
        static const uint32_t name_off[] = {0, 1, 7, 13, 23};
        uint64_t off = (i == 4) ? shstr_off : (uint64_t)(i * 0x10);
        uint64_t sz = (i == 4) ? (uint64_t)shstr_len : 0x10;
        sh[0] = (uint8_t)name_off[i]; /* sh_name */
        sh[4] = 1;                    /* sh_type = PROGBITS */
        memcpy(sh + 24, &off, 8);     /* sh_offset */
        memcpy(sh + 32, &sz, 8);      /* sh_size   */
    }

    /* 节名字符串表 @0x300 */
    memcpy(b + shstr_off, shstr, shstr_len);
    /* 写文件，文件总长需覆盖 shoff 区与 shstr 区 */
    f = fopen(path, "wb");
    if (!f) return -1;
    p = fwrite(b, 1, shstr_off + shstr_len, f);
    fclose(f);
    return (p == shstr_off + shstr_len) ? 0 : -1;
}

/* 合成一个 Mach-O 大端魔数文件 */
static void write_macho(const char *path) {
    uint8_t m[8] = {0xce, 0xfa, 0xed, 0xfe, 0x07, 0, 0, 0};
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(m, 1, sizeof(m), f);
        fclose(f);
    }
}

static void write_pe(const char *path) {
    uint8_t m[8] = {'M', 'Z', 0x90, 0, 0, 0, 0, 0};
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(m, 1, sizeof(m), f);
        fclose(f);
    }
}

int main(void) {
    void *so;
    hw_plugin_t *(*entry)(void);
    hw_plugin_t *p = NULL;
    hw_loader_ops_t *ops = NULL;
    const char *synth_elf = "_synth.elf";

    setbuf(stdout, NULL); /* 关闭缓冲，确保输出立即可见 */

    printf("== LOADER 插件 dlopen 自测 ==\n");

    so = dlopen("./build/loader.so", RTLD_NOW);
    if (!so) {
        printf("dlopen 失败: %s\n", dlerror());
        return 1;
    }
    *(void **)(&entry) = dlsym(so, "hw_plugin_entry");
    CHECK(entry != NULL, "hw_plugin_entry 符号存在");
    if (!entry) return 1;
    p = entry();
    CHECK(p && strcmp(p->id, "loader") == 0, "插件 id == loader");
    CHECK(p && p->type == HWPLUGIN_TYPE_LOADER, "插件类型 == LOADER");
    ops = (hw_loader_ops_t *)p->ops.get_interface("LOADER");
    CHECK(ops != NULL, "get_interface(LOADER) 返回接口");
    if (!ops) return 1;

    /* ---- 1. 格式识别 ---- */
    printf("\n-- 1) 格式识别 --\n");
    {
        loader_format_t fmt;
        char nm[32];
        write_pe("_t_pe.bin");
        CHECK(ops->detect("_t_pe.bin", &fmt, nm, sizeof(nm), NULL, 0) == 0 &&
                  fmt == LOADER_FORMAT_PE,
              "PE(MZ) -> PE/COFF");
        remove("_t_pe.bin");
    }
    {
        loader_format_t fmt;
        char nm[32];
        CHECK(write_synth_elf(synth_elf) == 0, "合成 ELF 写入成功");
        CHECK(ops->detect(synth_elf, &fmt, nm, sizeof(nm), NULL, 0) == 0 &&
                  fmt == LOADER_FORMAT_ELF,
              "ELFMAGIC -> ELF");
        printf("     _synth.elf -> %s\n", nm);
    }
    {
        loader_format_t fmt;
        write_macho("_t_macho.bin");
        CHECK(ops->detect("_t_macho.bin", &fmt, NULL, 0, NULL, 0) == 0 &&
                  fmt == LOADER_FORMAT_MACHO,
              "Mach-O 大端 -> Mach-O");
        remove("_t_macho.bin");
    }
    {
        loader_format_t fmt;
        const char *sh_path = "_t.sh";
        FILE *f = fopen(sh_path, "wb");
        if (f) {
            fputs("#!/bin/echo\n", f);
            fclose(f);
        }
        char interp[64] = {0};
        CHECK(ops->detect(sh_path, &fmt, NULL, 0, interp, sizeof(interp)) == 0 &&
                  fmt == LOADER_FORMAT_SCRIPT && strstr(interp, "/bin/echo"),
              "shebang -> SCRIPT 且解释器=/bin/echo");
        remove(sh_path);
    }
    {
        loader_format_t fmt;
        const char *nul_path = "_t_null.bin";
        FILE *f = fopen(nul_path, "wb");
        if (f) {
            fclose(f);
        }
        CHECK(ops->detect(nul_path, &fmt, NULL, 0, NULL, 0) < 0 && fmt == LOADER_FORMAT_UNKNOWN,
              "空文件 -> 负 errno(无崩溃)");
        remove(nul_path);
    }

    /* ---- 2. 合成 ELF 头解析 ---- */
    printf("\n-- 2) ELF 头解析 --\n");
    {
        loader_elf_info_t info;
        int r = ops->elf_parse(synth_elf, &info);
        CHECK(r == 0, "elf_parse(_synth.elf) 成功");
        printf("     class=%u e_type=%u e_machine=%u entry=0x%llx\n", info.ei_class, info.e_type,
               info.e_machine, (unsigned long long)info.e_entry);
        printf("     ph_count=%u has_interp=%d sections=%s\n", info.ph_count, info.has_interp,
               info.section_names);
        CHECK(info.ei_class == 2, "ei_class == ELFCLASS64");
        CHECK(info.e_type == 2, "e_type == ET_EXEC");
        CHECK(info.e_machine == 0x3e, "e_machine == EM_X86_64");
        CHECK(info.e_entry == 0x40000000, "e_entry == 0x40000000");
        CHECK(info.ph_count == 2, "解析到 2 个程序头");
        CHECK(info.has_interp == 1, "检测到 PT_INTERP");
        CHECK(strstr(info.section_names, ".text") && strstr(info.section_names, ".data"),
              "节名概要含 .text/.data");
    }

    /* ---- 3. 列出支持格式 ---- */
    printf("\n-- 3) 支持的格式 --\n");
    {
        loader_format_info_t fmts[16];
        int n = ops->get_formats(fmts, 16);
        printf("     共 %d 种格式:\n", n);
        for (int i = 0; i < n && i < 16; i++)
            printf("       - %-22s magic=%-6s ext=%s\n", fmts[i].name, fmts[i].magic,
                   fmts[i].extension);
        CHECK(n >= 6, "格式列表 >= 6 种");
    }

    /* ---- 4. run：script 经 shebang 解释器 fork+exec ---- */
    printf("\n-- 4) run 运行 shebang 脚本 --\n");
    {
        FILE *rl = fopen("selftest/_runlog.txt", "w");
        if (rl) {
            setvbuf(rl, NULL, _IONBF, 0); /* 立即落盘，避免子进程干扰缓冲 */
            fprintf(rl, "[runlog] started\n");
        }
        {
            const char *sh_path = "_run.sh";
            int exit_code = -999;
            FILE *f = fopen(sh_path, "wb");
            if (f) {
                fputs("#!/bin/echo\n", f);
                fclose(f);
            }
            char *argv[] = {NULL};
            int r = ops->run(sh_path, argv, 1, &exit_code);
            CHECK(r == 0 && exit_code == 0, "脚本解释器运行成功且退出码 0");
            if (rl) fprintf(rl, "run(_run.sh)    -> 返回=%d exit_code=%d\n", r, exit_code);
            remove(sh_path);
        }
        {
            int exit_code = 0;
            int r = ops->run("/bin/echo", NULL, 1, &exit_code);
            CHECK(r < 0, "本机非 ELF 格式(PE)返回负 errno（不伪执行）");
            if (rl) fprintf(rl, "run(PE://bin/echo)-> 返回=%d (期望负值)\n", r);
        }
        if (rl) {
            fprintf(rl, "[runlog] failed=%d\n", failures);
            fclose(rl);
        }
        printf("     run 结果已写入 _runlog.txt\n");
    }

    remove(synth_elf);
    dlclose(so);
    printf("\n== 自测结束: %d 项失败 ==\n", failures);
    return failures ? 2 : 0;
}