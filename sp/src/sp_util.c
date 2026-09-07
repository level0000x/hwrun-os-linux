/*
 * sp_util.c — SP 插件内部工具
 *
 * 提供临时文件、openssl 命令封装、十六进制/base64 编解码、
 * 常量时间比较等通用基础设施。
 *
 * 说明：系统未安装 libcrypto 开发库时，真实加解密/哈希/密钥/签名
 * 均封装本机 `openssl` 可执行文件（本环境验证可用），数据与密钥始终
 * 经临时文件/标准输入传递，绝不把密钥明文放到命令行参数上。
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>

#include "sp_internal.h"
#include "sp.h"

sp_ctx_t g_sp_ctx;

/* ============================================================
 * 当前毫秒时间戳
 * ============================================================ */
uint64_t sp_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* ============================================================
 * 临时目录：优先 SP_TMPDIR，否则 TMPDIR，否则 /tmp
 * ============================================================ */
static const char *sp_tmpdir(void) {
    const char *d;
    static char def[] = "/tmp";
    d = getenv(SP_TMPDIR_ENV);
    if (d && *d) return d;
    d = getenv("TMPDIR");
    if (d && *d) return d;
    return def;
}

/* ============================================================
 * 创建唯一临时文件路径（基于 mkstemp，安全）
 * ============================================================ */
char *sp_tmp_path(char *buf, size_t cap) {
    const char *dir = sp_tmpdir();
    int len = snprintf(buf, cap, "%s/spXXXXXX", dir);
    if (len < 0 || (size_t)len >= cap) return NULL;
    int fd = mkstemp(buf);
    if (fd < 0) return NULL;
    /* 立即关闭；调用方会用 fopen/重定向覆盖写 */
    close(fd);
    unlink(buf);  /* 使得路径存在但未创建，需再次以 O_CREAT 打开 */
    return buf;
}

/* ============================================================
 * 写入文件（截断）
 * ============================================================ */
int sp_write_all(const char *path, const void *data, size_t len) {
    if (!path || !data) return SP_EINVAL;
    FILE *f = fopen(path, "wb");
    if (!f) return SP_EIO;
    size_t n = fwrite(data, 1, len, f);
    int rc = (n == len) ? SP_OK : SP_EIO;
    if (fclose(f) != 0 && rc == SP_OK) rc = SP_EIO;
    return rc;
}

/* ============================================================
 * 简洁读取文本文件
 * ============================================================ */
int sp_read_text(const char *path, char *buf, size_t cap) {
    if (!path || !buf || cap == 0) return SP_EINVAL;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, cap - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = 0;
    return (int)n;
}

/* ============================================================
 * shell 单引号包装
 * 将字符串包成 '...'，内部的 ' 替换为 '"'"'，防止注入。
 * ============================================================ */
const char *sp_q(const char *s, char *buf, size_t cap) {
    if (!s || !buf || cap == 0) return NULL;
    size_t i = 0;
    buf[i++] = '\'';
    for (; *s && i + 6 < cap; s++) {
        if (*s == '\'') {
            memcpy(buf + i, "'\\''", 4);
            i += 4;
        } else {
            buf[i++] = *s;
        }
    }
    if (i + 2 > cap) return NULL;
    buf[i++] = '\'';
    buf[i] = 0;
    return buf;
}

/* ============================================================
 * 运行命令，捕获退出码；可选重定向 stdout/stderr
 * 返回进程退出码（0=NOLOG 成功）；命令本身无法启动返回 -1。
 * ============================================================ */
int sp_sh(const char *cmd, const char *stdout_file, const char *stderr_file) {
    if (!cmd) return SP_EINVAL;
    char qo[1024], qe[1024];
    char full[8192];
    int n = snprintf(full, sizeof(full), "%s", cmd);
    if (stdout_file && *stdout_file) {
        const char *q = sp_q(stdout_file, qo, sizeof(qo));
        n += snprintf(full + n, sizeof(full) - (size_t)n, " > %s", q);
    }
    if (stderr_file && *stderr_file) {
        const char *q = sp_q(stderr_file, qe, sizeof(qe));
        n += snprintf(full + n, sizeof(full) - (size_t)n, " 2> %s", q);
    }
    int rc = system(full);
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    if (WIFSIGNALED(rc)) return -1;
    return rc;
}

/* ============================================================
 * 十六进制编码（out 需 >= 2n+1）
 * ============================================================ */
void sp_to_hex(const uint8_t *in, size_t in_len, char *out) {
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < in_len; i++) {
        out[i * 2]     = d[in[i] >> 4];
        out[i * 2 + 1] = d[in[i] & 0x0f];
    }
    out[in_len * 2] = 0;
}

/* ============================================================
 * 十六进制解码
 * ============================================================ */
int sp_from_hex(const char *hex, uint8_t *out, size_t out_cap, size_t *out_len) {
    if (!hex || !out) return SP_EINVAL;
    size_t n = strlen(hex);
    if (n % 2) return SP_EINVAL;
    n /= 2;
    if (n > out_cap) return SP_ENOMEM;
    for (size_t i = 0; i < n; i++) {
        unsigned int hi = 0, lo = 0;
        char a = hex[i * 2], b = hex[i * 2 + 1];
        if      (a >= '0' && a <= '9') hi = a - '0';
        else if (a >= 'a' && a <= 'f') hi = a - 'a' + 10;
        else if (a >= 'A' && a <= 'F') hi = a - 'A' + 10;
        else return SP_EINVAL;
        if      (b >= '0' && b <= '9') lo = b - '0';
        else if (b >= 'a' && b <= 'f') lo = b - 'a' + 10;
        else if (b >= 'A' && b <= 'F') lo = b - 'A' + 10;
        else return SP_EINVAL;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    if (out_len) *out_len = n;
    return SP_OK;
}

/* ============================================================
 * base64 编码（调用 openssl base64，-A 单行）
 * ============================================================ */
int sp_b64_encode(const uint8_t *in, size_t in_len, char *out, size_t cap) {
    if (!in || !out || cap == 0) return SP_EINVAL;
    if (in_len > 0x3ffff) return SP_ENOMEM;  /* 上限 256KB，防临时文件滥用 */

    char pip[8192], pout[8192], qbuf[8192];
    if (in_len > sizeof(pip) - 1) return SP_ENOMEM;
    memcpy(pip, in, in_len);           /* 备用输入，避免命令读取未定文件 */
    char *inpath = sp_tmp_path(pip, sizeof(pip));
    if (!inpath) return SP_EIO;
    if (sp_write_all(inpath, in, in_len) != SP_OK) return SP_EIO;

    char cmd[16384];
    const char *qq = sp_q(inpath, qbuf, sizeof(qbuf));
    snprintf(cmd, sizeof(cmd), "openssl base64 -A -in %s", qq);
    char *outpath = sp_tmp_path(pout, sizeof(pout));
    int rc = sp_sh(cmd, outpath, NULL);
    if (rc != 0) { unlink(inpath); return SP_EIO; }

    char enc[8192];
    ssize_t n = sp_read_text(outpath, enc, sizeof(enc));
    unlink(inpath); unlink(outpath);
    if (n <= 0) return SP_EIO;
    while (n > 0 && (enc[n-1] == '\n' || enc[n-1] == '\r' || enc[n-1] == ' ')) enc[--n] = 0;
    if ((size_t)n + 1 >= cap) return SP_ENOMEM;
    memcpy(out, enc, (size_t)n + 1);
    return SP_OK;
}

/* ============================================================
 * base64 解码
 * ============================================================ */
int sp_b64_decode(const char *b64, uint8_t *out, size_t cap, size_t *out_len) {
    if (!b64 || !out) return SP_EINVAL;
    if (strlen(b64) > 0x3ffff) return SP_ENOMEM;

    char pip[8192], pout[8192], qbuf[8192];
    char *inpath = sp_tmp_path(pip, sizeof(pip));
    if (!inpath) return SP_EIO;
    if (sp_write_all(inpath, b64, strlen(b64)) != SP_OK) return SP_EIO;

    char cmd[16384];
    const char *qq = sp_q(inpath, qbuf, sizeof(qbuf));
    snprintf(cmd, sizeof(cmd), "openssl base64 -d -A -in %s", qq);
    char *outpath = sp_tmp_path(pout, sizeof(pout));
    int rc = sp_sh(cmd, outpath, NULL);
    if (rc != 0) { unlink(inpath); return SP_EIO; }
    ssize_t n = sp_read_text(outpath, (char *)out, cap);
    unlink(inpath); unlink(outpath);
    if (n < 0) return SP_EIO;
    if (out_len) *out_len = (size_t)n;
    return SP_OK;
}

/* ============================================================
 * 常量时间相等比较（防时序侧信道）
 * ============================================================ */
int sp_ct_eq(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0 ? 0 : 1;
}