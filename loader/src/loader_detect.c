/*
 * loader_detect.c — 格式检测与路由
 *
 * 读取文件头（前 64 字节），依据魔数表 / shebang 识别可执行文件格式，
 * 并提供"列出支持格式"的实现。全部为纯 C、无崩溃路径。
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "loader.h"

/* ============================================================
 * 各格式魔数定义
 * ============================================================ */
#define MAGIC_ELF     "\x7f\x45\x4c\x46"   /* 0x7f 'E' 'L' 'F'  */
#define MAGIC_PE      "\x4d\x5a"           /* 'M' 'Z'            */
#define MAGIC_MACHO32 "\xfe\xed\xfa\xce"
#define MAGIC_MACHO64 "\xfe\xed\xfa\xcf"
#define MAGIC_MACHOBE "\xce\xfa\xed\xfe"
#define MAGIC_APK     "\x50\x4b\x03\x04"   /* ZIP                 */
#define MAGIC_WASM    "\x00\x61\x73\x6d"   /* '\0' 'a' 's' 'm'    */

/* 魔数表条目 */
typedef struct magic_entry {
    loader_format_t format;
    const char      *name;
    const char      *magic;
    uint32_t         magic_len;
    const char      *ext;
} magic_entry_t;

static const magic_entry_t magic_table[] = {
    { LOADER_FORMAT_ELF,    "ELF",      MAGIC_ELF,     4, ""    },
    { LOADER_FORMAT_PE,     "PE/COFF",  MAGIC_PE,      2, ".exe" },
    { LOADER_FORMAT_MACHO,  "Mach-O",   MAGIC_MACHO64, 4, ""    },
    { LOADER_FORMAT_WASM,   "WASM",     MAGIC_WASM,    4, ".wasm" },
    { LOADER_FORMAT_APK,    "APK",      MAGIC_APK,     4, ".apk" },
    { LOADER_FORMAT_JAVA,   "Java",     "\xca\xfe\xba\xbe", 4, ".class" },
};

#define MAGIC_COUNT (sizeof(magic_table) / sizeof(magic_entry_t))

/* ============================================================
 * 读取文件内容到缓冲区，返回读取字节数（<=cap），失败返回负 errno
 * ============================================================ */
static long loader_read_head(const char *path, uint8_t *buf, size_t cap) {
    FILE *f;
    size_t n;
    if (!path || !buf) return -EINVAL;
    f = fopen(path, "rb");
    if (!f) return -errno;
    n = fread(buf, 1, cap, f);
    fclose(f);
    return (long)n;
}

/* ============================================================
 * 格式检测实现：读取文件头 → 匹配魔数 / shebang
 * 成功返回 0；文件不可读返回负 errno；格式可识别则 fmt 被填充。
 * ============================================================ */
int loader_detect_impl(const char *path, loader_format_t *fmt,
                       char *format_name, size_t name_size,
                       char *interpreter, size_t interp_size) {
    uint8_t hdr[64];
    long rlen;
    int i;

    if (fmt) *fmt = LOADER_FORMAT_UNKNOWN;

    rlen = loader_read_head(path, hdr, sizeof(hdr));
    if (rlen < 0) return (int)rlen;
    if (rlen == 0) return -ENOEXEC;      /* 空文件 */

    /* 1) shebang 脚本：#!/path/to/interp */
    if (rlen >= 2 && hdr[0] == '#' && hdr[1] == '!') {
        /* 提取解释器路径（第一行、第一个空白前） */
        if (interpreter && interp_size > 0) {
            const char *s = (const char *)hdr + 2;
            const char *end = s;
            while ((size_t)(end - (const char *)hdr) < (size_t)rlen &&
                   *end && *end != '\n' && *end != ' ' && *end != '\t')
                end++;
            size_t ilen = (size_t)(end - s);
            if (ilen > 0 && ilen < interp_size - 1) {
                memcpy(interpreter, s, ilen);
                interpreter[ilen] = '\0';
            } else {
                interpreter[0] = '\0';
            }
        }
        if (format_name && name_size > 0) {
            snprintf(format_name, name_size, "Script");
        }
        if (fmt) *fmt = LOADER_FORMAT_SCRIPT;
        return 0;
    }

    /* 2) Mach-O 大端也在表中一并匹配 */
    if (rlen >= 4 && memcmp(hdr, MAGIC_MACHOBE, 4) == 0) {
        if (fmt) *fmt = LOADER_FORMAT_MACHO;
        if (format_name && name_size) snprintf(format_name, name_size, "Mach-O");
        if (interpreter && interp_size) interpreter[0] = '\0';
        return 0;
    }

    /* 3) 遍历魔数表 */
    for (i = 0; i < (int)MAGIC_COUNT; i++) {
        const magic_entry_t *e = &magic_table[i];
        if ((long)e->magic_len <= rlen &&
            memcmp(hdr, e->magic, e->magic_len) == 0) {
            if (fmt) *fmt = e->format;
            if (format_name && name_size)
                snprintf(format_name, name_size, "%s", e->name);
            if (interpreter && interp_size) interpreter[0] = '\0';
            return 0;
        }
    }

    /* 4) 无法识别 */
    if (format_name && name_size) snprintf(format_name, name_size, "Unknown");
    if (fmt) *fmt = LOADER_FORMAT_UNKNOWN;
    return -ENOEXEC;
}

/* ============================================================
 * 列出支持的格式
 * ============================================================ */
int loader_formats_impl(loader_format_info_t *out, int cap) {
    int i, n = 0;
    if (!out || cap <= 0) return -EINVAL;

    /* 先登记脚本，再登记魔数表，最后补 Mach-O 大端提示 */
    if (n < cap) {
        out[n].format = LOADER_FORMAT_SCRIPT;
        snprintf(out[n].name, sizeof(out[n].name), "Script");
        snprintf(out[n].magic, sizeof(out[n].magic), "#!"); /* shebang */
        out[n].magic_len = 2;
        out[n].extension[0] = '\0';
        out[n].supported = 1;
        n++;
    }
    for (i = 0; i < (int)MAGIC_COUNT && n < cap; i++) {
        const magic_entry_t *e = &magic_table[i];
        out[n].format = e->format;
        snprintf(out[n].name, sizeof(out[n].name), "%s", e->name);
        snprintf(out[n].magic, sizeof(out[n].magic), "%s", e->magic);
        out[n].magic_len = e->magic_len;
        snprintf(out[n].extension, sizeof(out[n].extension), "%s", e->ext);
        out[n].supported = 1;
        n++;
    }
    if (n < cap) { /* 补 Mach-O 大端别名的提示 */
        out[n].format = LOADER_FORMAT_MACHO;
        snprintf(out[n].name, sizeof(out[n].name), "Mach-O(big-endian)");
        snprintf(out[n].magic, sizeof(out[n].magic), "\xce\xfa\xed\xfe");
        out[n].magic_len = 4;
        out[n].extension[0] = '\0';
        out[n].supported = 1;
        n++;
    }
    return n;
}