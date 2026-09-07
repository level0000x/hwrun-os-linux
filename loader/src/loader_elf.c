/*
 * loader_elf.c — ELF 头解析
 *
 * 读取 ELF 文件头，解析 ei_class/e_data/e_type/e_machine/e_entry 等真实字段，
 * 并提取程序头（PT_*）概要及节名字符串表概要。支持 32/64 位及大小端。
 * 仅做只读解析，不加载 / 不执行；失败返回负 errno。
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>

#include "loader.h"

/* ---- ELF 常量 ---- */
#define ELFCLASS32 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

#define PT_NULL     0
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_NOTE     4
#define PT_PHDR     6

/* ---- 小工具：按大小端读取不同宽度整数 ---- */
static uint16_t rd16(const uint8_t *p, int be) {
    return be ? (uint16_t)((p[0] << 8) | p[1])
              : (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd32(const uint8_t *p, int be) {
    return be ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                ((uint32_t)p[2] << 8)  | (uint32_t)p[3]
              : (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p, int be) {
    if (be) {
        uint64_t v = 0; int i;
        for (i = 0; i < 8; i++) v = (v << 8) | p[i];
        return v;
    }
    uint64_t v = 0; int i;
    for (i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

/* ---- ELF32/64 头与程序头/节头 —— 字节序无关的内存视图 ---- */
typedef struct {
    uint8_t  ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum;
    uint16_t e_shentsize, e_shnum, e_shstrndx;
} ehdr_view_t;

typedef struct {
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr;
    uint64_t p_filesz, p_memsz, p_align;
} phdr_view_t;

typedef struct {
    uint32_t sh_name, sh_type;
    uint64_t sh_flags, sh_addr, sh_offset, sh_size;
    uint32_t sh_link, sh_info;
    uint64_t sh_addralign, sh_entsize;
} shdr_view_t;

/* 依 ei_data 决定端序 */
static int want_be(const uint8_t *id) {
    return id[5] == ELFDATA2MSB;
}

/* ---- 读取整个文件到内存（限制大小防止滥用） ---- */
static int read_all(const char *path, uint8_t **out, long *out_len) {
    FILE *f;
    long sz;
    uint8_t *buf;
    f = fopen(path, "rb");
    if (!f) return -errno;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -EINVAL; }
    sz = ftell(f);
    if (sz < 0) { fclose(f); return -EIO; }
    if (sz > 64L * 1024 * 1024) { fclose(f); return -EFBIG; } /* 过大不解析 */
    rewind(f);
    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return -ENOMEM; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return -EIO;
    }
    fclose(f);
    *out = buf;
    *out_len = sz;
    return 0;
}

/* 依 class 单独解析 ELF32 程序头（结构更小） */
static int parse_ph32(const uint8_t *m, long len, int be,
                      const ehdr_view_t *eh, loader_elf_info_t *out) {
    int i;
    uint16_t phnum = eh->e_phnum;
    if (!phnum || eh->e_phoff == 0) return 0;
    for (i = 0; i < phnum && i < 8; i++) {
        const uint8_t *p = m + eh->e_phoff + (long)i * 32; /* ELF32 phdr=32B */
        loader_ph_info_t *dst = &out->ph[out->ph_count];
        if (p + 32 > m + len) break;
        dst->p_type   = rd32(p + 0, be);
        dst->p_offset = rd32(p + 4, be);
        dst->p_vaddr  = rd32(p + 8, be);
        dst->p_paddr  = rd32(p + 12, be);
        dst->p_filesz = rd32(p + 16, be);
        dst->p_memsz  = rd32(p + 20, be);
        dst->p_flags  = rd32(p + 24, be);
        if (dst->p_type == PT_INTERP) out->has_interp = 1;
        out->ph_count++;
    }
    return 0;
}

static int parse_ph64(const uint8_t *m, long len, int be,
                      const ehdr_view_t *eh, loader_elf_info_t *out) {
    int i;
    uint16_t phnum = eh->e_phnum;
    if (!phnum || eh->e_phoff == 0) return 0;
    for (i = 0; i < phnum && i < 8; i++) {
        const uint8_t *p = m + eh->e_phoff + (long)i * 56; /* ELF64 phdr=56B */
        loader_ph_info_t *dst = &out->ph[out->ph_count];
        if (p + 56 > m + len) break;
        dst->p_type   = rd32(p + 0, be);
        dst->p_flags  = rd32(p + 4, be);
        dst->p_offset = rd64(p + 8, be);
        dst->p_vaddr  = rd64(p + 16, be);
        dst->p_paddr  = rd64(p + 24, be);
        dst->p_filesz = rd64(p + 32, be);
        dst->p_memsz  = rd64(p + 40, be);
        if (dst->p_type == PT_INTERP) out->has_interp = 1;
        out->ph_count++;
    }
    return 0;
}

/* ---- 读取并解析节名字符串表 ---- */
static void parse_sections(const uint8_t *m, long len, int be,
                           const ehdr_view_t *eh, loader_elf_info_t *out) {
    int cls = out->ei_class;
    uint16_t shnum  = eh->e_shnum;
    uint16_t shstrn = eh->e_shstrndx;
    size_t shsz, strsz;
    uint64_t shoff, stroff;
    char tmp[64];
    int i;

    if (cls == ELFCLASS64) { shsz = 64; shoff = eh->e_shoff; }
    else                   { shsz = 40; shoff = eh->e_shoff; }

    /* 定位 shstrtab 表：其 index 在 e_shstrndx，先读该节头获得偏移与大小 */
    if (shstrn != 0 && shoff && shstrn < shnum) {
        const uint8_t *sh = m + shoff + (long)shstrn * (long)shsz;
        if (sh + shsz <= m + len) {
            if (cls == ELFCLASS64) {
                stroff = rd64(sh + 24, be);  /* sh_offset */
                strsz  = rd64(sh + 32, be);  /* sh_size   */
            } else {
                stroff = rd32(sh + 16, be);
                strsz  = rd32(sh + 20, be);
            }
        } else { stroff = 0; strsz = 0; }
    } else { stroff = 0; strsz = 0; }

    /* 遍历各节头取名字，拼接概要 */
    out->section_names[0] = '\0';
    {
        size_t pos = 0;
        for (i = 0; i < shnum && i < 40; i++) {
            const uint8_t *sh = m + shoff + (long)i * (long)shsz;
            uint64_t nameoff;
            const uint8_t *nstr;
            size_t nlen = 0;
            if (sh + shsz > m + len) break;
            nameoff = rd32(sh + 0, be);   /* sh_name 对两种 class 均为 4 字节 */
            /* 越过字符串表检查 nameoff 范围 */
            if (stroff && strsz && nameoff < strsz) {
                nstr = m + stroff + nameoff;
                while ((size_t)(nstr + nlen) < (size_t)m + (size_t)len &&
                       nstr[nlen] != '\0' && nlen < sizeof(tmp) - 1)
                    nlen++;
            } else {
                nstr = NULL;
            }
            if (nlen > 0) {
                memcpy(tmp, nstr, nlen);
                tmp[nlen] = '\0';
                if (pos == 0) {
                    pos += (size_t)snprintf(out->section_names + pos,
                                            sizeof(out->section_names) - pos,
                                            "%s", tmp);
                } else if (pos + nlen + 2 < sizeof(out->section_names)) {
                    pos += (size_t)snprintf(out->section_names + pos,
                                            sizeof(out->section_names) - pos,
                                            ",%s", tmp);
                }
            }
        }
    }
}

/* ============================================================
 * ELF 解析实现 <- elf_parse 协议方法
 * ============================================================ */
int loader_elf_impl(const char *path, loader_elf_info_t *out) {
    uint8_t *m = NULL;
    long len = 0;
    ehdr_view_t eh;
    int be;
    int ret;

    if (!path || !out) return -EINVAL;
    memset(out, 0, sizeof(*out));

    ret = read_all(path, &m, &len);
    if (ret < 0) return ret;
    if (len < 52 || memcmp(m, "\x7f\x45\x4c\x46", 4) != 0) {
        free(m);
        return -ENOEXEC;              /* 不是 ELF */
    }

    be = want_be(m + 1);
    memset(&eh, 0, sizeof(eh));
    memcpy(eh.ident, m, 16);
    eh.e_type     = rd16(m + 16, be);
    eh.e_machine  = rd16(m + 18, be);
    eh.e_version  = rd32(m + 20, be);
    eh.e_flags    = (uint32_t)rd32(m + 48, be);
    eh.e_ehsize   = rd16(m + 52, be);
    eh.e_phentsize= rd16(m + 54, be);
    eh.e_phnum    = rd16(m + 56, be);
    eh.e_shentsize= rd16(m + 58, be);
    eh.e_shnum    = rd16(m + 60, be);
    eh.e_shstrndx = rd16(m + 62, be);

    out->ei_class = m[4];
    out->ei_data  = m[5];
    out->e_type   = eh.e_type;
    out->e_machine= eh.e_machine;
    out->e_flags  = eh.e_flags;
    out->e_phnum  = eh.e_phnum;
    out->e_shnum  = eh.e_shnum;
    out->e_shstrndx = eh.e_shstrndx;

    /* 入口点与偏移随 class 不同 */
    if (out->ei_class == ELFCLASS32) {
        if (len >= 32) eh.e_entry = rd32(m + 24, be);
        if (len >= 36) eh.e_phoff = rd32(m + 28, be);
        if (len >= 40) eh.e_shoff = rd32(m + 32, be);
    } else { /* 64 */
        if (len >= 32) eh.e_entry = rd64(m + 24, be);
        if (len >= 40) eh.e_phoff = rd64(m + 32, be);
        if (len >= 48) eh.e_shoff = rd64(m + 40, be);
    }
    out->e_entry = eh.e_entry;

    /* 程序头概要 */
    if (eh.e_phnum && eh.e_phoff) {
        if (out->ei_class == ELFCLASS64)
            parse_ph64(m, len, be, &eh, out);
        else
            parse_ph32(m, len, be, &eh, out);
    }

    /* 节名概要 */
    if (eh.e_shnum && eh.e_shoff)
        parse_sections(m, len, be, &eh, out);

    free(m);
    return 0;
}