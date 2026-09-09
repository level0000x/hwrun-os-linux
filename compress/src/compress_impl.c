/*
 * compress_impl.c — COMPRESS 协议实现（真实 zlib + libzstd）
 *
 * v1 引擎：
 *   - gzip / zlib：zlib deflate/inflate（deflateInit2 windowBits 15|15+16；
 *     解压统一 windowBits 32+15 自动识别两种容器）；
 *   - zstd：libzstd 缓冲/流式；
 *   - xz / lz4：不在 v1 链接，compress/decompress 返回 -ENOTSUP，
 *     detect/list 仍可识别其魔数（能力表标注 supported=0）。
 *
 * 错误约定：成功返回 0；失败返回负 errno。
 * 缓冲解压长度未知，输出由本实现动态增长并 malloc，调用方 free_result 释放。
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <zlib.h>
#include <zstd.h>

#include "compress.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 供 ops 表引用的内部实现（前置声明，避免 -Wmissing-prototypes） */
static int compress_detect(const uint8_t *d, uint32_t len, int *algo);
static int compress_buffer(int algo, int level, const uint8_t *in, uint32_t in_len,
                           hw_compress_result_t *res);
static int compress_decompress(int force_algo, const uint8_t *in, uint32_t in_len,
                               hw_compress_result_t *res);

#define CHUNK 65536

/* ============================================================
 * 级别映射
 * ============================================================ */
static int zlib_level(int lvl) {
    switch (lvl) {
    case HW_COMPRESS_LEVEL_FASTEST:
        return 1;
    case HW_COMPRESS_LEVEL_FAST:
        return 3;
    case HW_COMPRESS_LEVEL_MORE:
        return 8;
    case HW_COMPRESS_LEVEL_BEST:
        return 9;
    case HW_COMPRESS_LEVEL_DEFAULT:
    default:
        return 6;
    }
}

static int zstd_level(int lvl) {
    switch (lvl) {
    case HW_COMPRESS_LEVEL_FASTEST:
        return 1;
    case HW_COMPRESS_LEVEL_FAST:
        return 3;
    case HW_COMPRESS_LEVEL_MORE:
        return 15;
    case HW_COMPRESS_LEVEL_BEST:
        return ZSTD_maxCLevel();
    case HW_COMPRESS_LEVEL_DEFAULT:
    default:
        return 5;
    }
}

/* ============================================================
 * 格式识别（魔数）
 * ============================================================ */
static int compress_detect(const uint8_t *d, uint32_t len, int *algo) {
    if (!d || !algo) return -EINVAL;
    *algo = HW_COMPRESS_ALGO_AUTO;
    if (len < 4) return 0;

    if (d[0] == 0x28 && d[1] == 0xB5 && d[2] == 0x2F && d[3] == 0xFD)
        *algo = HW_COMPRESS_ALGO_ZSTD;
    else if (d[0] == 0x1F && d[1] == 0x8B)
        *algo = HW_COMPRESS_ALGO_GZIP;
    else if (d[0] == 0xFD && d[1] == 0x37 && d[2] == 0x7A)
        *algo = HW_COMPRESS_ALGO_XZ;
    else if (d[0] == 0x04 && d[1] == 0x22 && d[2] == 0x4D && d[3] == 0x18)
        *algo = HW_COMPRESS_ALGO_LZ4;
    else if (d[0] == 0x78 && (d[1] == 0x01 || d[1] == 0x9C || d[1] == 0xDA || d[1] == 0x5E))
        *algo = HW_COMPRESS_ALGO_ZLIB;
    return 0;
}

/* ============================================================
 * 缓冲压缩：zlib 容器（gzip 或 zlib）
 * ============================================================ */
static int buf_compress_zlib(int is_gzip, int level, const uint8_t *in, uint32_t in_len,
                             uint8_t **out, uint32_t *out_len) {
    z_stream s;
    memset(&s, 0, sizeof(s));
    if (deflateInit2(&s, zlib_level(level), Z_DEFLATED, is_gzip ? (15 + 16) : 15, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        return -ENOMEM;

    uLong bound = deflateBound(&s, in_len);
    uint8_t *buf = (uint8_t *)malloc(bound ? bound : 1);
    if (!buf) {
        deflateEnd(&s);
        return -ENOMEM;
    }
    s.next_in = (Bytef *)in;
    s.avail_in = in_len;
    s.next_out = buf;
    s.avail_out = (uInt)bound;
    int rc = deflate(&s, Z_FINISH);
    deflateEnd(&s);
    if (rc != Z_STREAM_END) {
        free(buf);
        return -EILSEQ;
    }
    *out = buf;
    *out_len = (uint32_t)s.total_out;
    return 0;
}

/* ============================================================
 * 缓冲压缩：zstd
 * ============================================================ */
static int buf_compress_zstd(int level, const uint8_t *in, uint32_t in_len, uint8_t **out,
                             uint32_t *out_len) {
    size_t bound = ZSTD_compressBound(in_len);
    uint8_t *buf = (uint8_t *)malloc(bound);
    if (!buf) return -ENOMEM;
    size_t n = ZSTD_compress(buf, bound, in, in_len, zstd_level(level));
    if (ZSTD_isError(n)) {
        free(buf);
        return -EILSEQ;
    }
    *out = buf;
    *out_len = (uint32_t)n;
    return 0;
}

static int compress_buffer(int algo, int level, const uint8_t *in, uint32_t in_len,
                           hw_compress_result_t *res) {
    if (!in || !res) return -EINVAL;
    memset(res, 0, sizeof(*res));

    uint8_t *out = NULL;
    uint32_t out_len = 0;
    int rc;
    switch (algo) {
    case HW_COMPRESS_ALGO_ZLIB:
        rc = buf_compress_zlib(0, level, in, in_len, &out, &out_len);
        break;
    case HW_COMPRESS_ALGO_GZIP:
        rc = buf_compress_zlib(1, level, in, in_len, &out, &out_len);
        break;
    case HW_COMPRESS_ALGO_ZSTD:
    case HW_COMPRESS_ALGO_AUTO:
        rc = buf_compress_zstd(level, in, in_len, &out, &out_len);
        algo = HW_COMPRESS_ALGO_ZSTD;
        break;
    case HW_COMPRESS_ALGO_XZ:
    case HW_COMPRESS_ALGO_LZ4:
    case HW_COMPRESS_ALGO_LZ4_RAW:
        return -ENOTSUP;
    default:
        return -EINVAL;
    }
    if (rc != 0) return rc;
    res->data = out;
    res->data_len = out_len;
    res->orig_len = in_len;
    res->algo = algo;
    return 0;
}

/* ============================================================
 * 缓冲解压：zlib/gzip（动态增长）
 * ============================================================ */
static int buf_decompress_zlib(const uint8_t *in, uint32_t in_len, uint8_t **out,
                               uint32_t *out_len) {
    z_stream s;
    memset(&s, 0, sizeof(s));
    if (inflateInit2(&s, 32 + 15) != Z_OK) return -ENOMEM;

    uint32_t cap = in_len > 4096 ? in_len * 2 + 1024 : 4096;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) {
        inflateEnd(&s);
        return -ENOMEM;
    }
    s.next_in = (Bytef *)in;
    s.avail_in = in_len;
    s.next_out = buf;
    s.avail_out = cap;
    for (;;) {
        if (s.avail_out == 0) {
            cap *= 2;
            uint8_t *nb = (uint8_t *)realloc(buf, cap);
            if (!nb) {
                free(buf);
                inflateEnd(&s);
                return -ENOMEM;
            }
            buf = nb;
            s.next_out = buf + s.total_out;
            s.avail_out = cap - (uInt)s.total_out;
        }
        int rc = inflate(&s, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) break;
        if (rc != Z_OK && rc != Z_BUF_ERROR) {
            free(buf);
            inflateEnd(&s);
            return -EILSEQ;
        }
    }
    inflateEnd(&s);
    *out = buf;
    *out_len = (uint32_t)s.total_out;
    return 0;
}

/* ============================================================
 * 缓冲解压：zstd（未知原长时流式增长）
 * ============================================================ */
static int buf_decompress_zstd(const uint8_t *in, uint32_t in_len, uint8_t **out,
                               uint32_t *out_len) {
    unsigned long long est = ZSTD_getFrameContentSize(in, in_len);
    uint32_t cap = (est != ZSTD_CONTENTSIZE_UNKNOWN && est != ZSTD_CONTENTSIZE_ERROR && est)
                       ? (uint32_t)est
                       : (in_len > 4096 ? in_len * 4 : 4096);
    uint8_t *buf = (uint8_t *)malloc(cap ? cap : 1);
    if (!buf) return -ENOMEM;

    ZSTD_DCtx *ctx = ZSTD_createDCtx();
    if (!ctx) {
        free(buf);
        return -ENOMEM;
    }
    ZSTD_inBuffer ib = {in, in_len, 0};
    uint32_t total = 0;
    for (;;) {
        ZSTD_outBuffer ob = {buf + total, cap - total, 0};
        size_t rc = ZSTD_decompressStream(ctx, &ob, &ib);
        total += (uint32_t)ob.pos;
        if (ZSTD_isError(rc)) {
            ZSTD_freeDCtx(ctx);
            free(buf);
            return -EILSEQ;
        }
        if (rc == 0) break;
        if (total == cap) {
            cap *= 2;
            uint8_t *nb = (uint8_t *)realloc(buf, cap);
            if (!nb) {
                ZSTD_freeDCtx(ctx);
                free(buf);
                return -ENOMEM;
            }
            buf = nb;
        }
    }
    ZSTD_freeDCtx(ctx);
    *out = buf;
    *out_len = total;
    return 0;
}

static int compress_decompress(int force_algo, const uint8_t *in, uint32_t in_len,
                               hw_compress_result_t *res) {
    if (!in || !res) return -EINVAL;
    memset(res, 0, sizeof(*res));

    int algo = force_algo;
    if (algo == HW_COMPRESS_ALGO_AUTO) {
        int rc = compress_detect(in, in_len, &algo);
        if (rc != 0) return rc;
    }
    uint8_t *out = NULL;
    uint32_t out_len = 0;
    int rc;
    switch (algo) {
    case HW_COMPRESS_ALGO_ZLIB:
    case HW_COMPRESS_ALGO_GZIP:
        rc = buf_decompress_zlib(in, in_len, &out, &out_len);
        break;
    case HW_COMPRESS_ALGO_ZSTD:
        rc = buf_decompress_zstd(in, in_len, &out, &out_len);
        break;
    case HW_COMPRESS_ALGO_XZ:
    case HW_COMPRESS_ALGO_LZ4:
    case HW_COMPRESS_ALGO_LZ4_RAW:
        return -ENOTSUP;
    default:
        return -EILSEQ;
    }
    if (rc != 0) return rc;
    res->data = out;
    res->data_len = out_len;
    res->orig_len = in_len;
    res->algo = algo;
    return 0;
}

/* ============================================================
 * 文件压缩：zlib/gzip（流式）
 * ============================================================ */
static int file_compress_zlib(FILE *in, FILE *out, int is_gzip, int level, uint64_t *n_out) {
    z_stream s;
    memset(&s, 0, sizeof(s));
    if (deflateInit2(&s, zlib_level(level), Z_DEFLATED, is_gzip ? (15 + 16) : 15, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        return -ENOMEM;
    uint8_t ibuf[CHUNK];
    uint8_t obuf[CHUNK];
    int last = 0;
    for (;;) {
        if (!last) {
            size_t rd = fread(ibuf, 1, sizeof(ibuf), in);
            if (rd < sizeof(ibuf)) last = 1;
            s.next_in = ibuf;
            s.avail_in = (uInt)rd;
        } else {
            s.next_in = Z_NULL;
            s.avail_in = 0;
        }
        for (;;) {
            s.next_out = obuf;
            s.avail_out = (uInt)sizeof(obuf);
            int rc = deflate(&s, last ? Z_FINISH : Z_NO_FLUSH);
            size_t prod = sizeof(obuf) - s.avail_out;
            if (prod) {
                if (fwrite(obuf, 1, prod, out) != prod) {
                    deflateEnd(&s);
                    return -EIO;
                }
                *n_out += prod;
            }
            if (rc == Z_STREAM_END) {
                deflateEnd(&s);
                return 0;
            }
            if (rc != Z_OK) {
                deflateEnd(&s);
                return -EILSEQ;
            }
            if (!last && s.avail_in == 0) break;
            /* last：持续 Z_FINISH 直到 STREAM_END */
        }
    }
}

/* ============================================================
 * 文件压缩：zstd（流式）
 * ============================================================ */
static int file_compress_zstd(FILE *in, FILE *out, int level, uint64_t *n_out) {
    ZSTD_CCtx *ctx = ZSTD_createCCtx();
    if (!ctx) return -ENOMEM;
    ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, zstd_level(level));
    uint8_t ibuf[CHUNK];
    uint8_t obuf[CHUNK];
    for (;;) {
        size_t rd = fread(ibuf, 1, sizeof(ibuf), in);
        int last = rd < sizeof(ibuf);
        ZSTD_inBuffer ib = {ibuf, rd, 0};
        for (;;) {
            ZSTD_outBuffer ob = {obuf, sizeof(obuf), 0};
            size_t rc = ZSTD_compressStream2(ctx, &ob, &ib, last ? ZSTD_e_end : ZSTD_e_continue);
            if (ZSTD_isError(rc)) {
                ZSTD_freeCCtx(ctx);
                return -EILSEQ;
            }
            if (ob.pos) {
                if (fwrite(obuf, 1, ob.pos, out) != ob.pos) {
                    ZSTD_freeCCtx(ctx);
                    return -EIO;
                }
                *n_out += ob.pos;
            }
            if (last && rc == 0) {
                ZSTD_freeCCtx(ctx);
                return 0;
            }
            if (!last && ib.pos == ib.size) break;
            /* last：持续 e_end 直到 rc==0 */
        }
    }
}

/* ============================================================
 * 文件解压：zlib/gzip（流式）
 * ============================================================ */
static int file_decompress_zlib(FILE *in, FILE *out, uint64_t *n_out) {
    z_stream s;
    memset(&s, 0, sizeof(s));
    if (inflateInit2(&s, 32 + 15) != Z_OK) return -ENOMEM;
    uint8_t ibuf[CHUNK];
    uint8_t obuf[CHUNK];
    int eof = 0;
    for (;;) {
        if (s.avail_in == 0) {
            if (eof) {
                inflateEnd(&s);
                return -EILSEQ; /* 截断：流未正常结束 */
            }
            size_t rd = fread(ibuf, 1, sizeof(ibuf), in);
            if (rd < sizeof(ibuf)) eof = 1;
            if (rd == 0 && !eof) continue;
            s.next_in = ibuf;
            s.avail_in = (uInt)rd;
        }
        s.next_out = obuf;
        s.avail_out = (uInt)sizeof(obuf);
        int rc = inflate(&s, Z_NO_FLUSH);
        size_t prod = sizeof(obuf) - s.avail_out;
        if (prod) {
            if (fwrite(obuf, 1, prod, out) != prod) {
                inflateEnd(&s);
                return -EIO;
            }
            *n_out += prod;
        }
        if (rc == Z_STREAM_END) {
            inflateEnd(&s);
            return 0;
        }
        if (rc != Z_OK && rc != Z_BUF_ERROR) {
            inflateEnd(&s);
            return -EILSEQ;
        }
    }
}

/* ============================================================
 * 文件解压：zstd（流式）
 * ============================================================ */
static int file_decompress_zstd(FILE *in, FILE *out, uint64_t *n_out) {
    ZSTD_DCtx *ctx = ZSTD_createDCtx();
    if (!ctx) return -ENOMEM;
    uint8_t ibuf[CHUNK];
    uint8_t obuf[CHUNK];
    ZSTD_inBuffer ib = {ibuf, 0, 0};
    for (;;) {
        if (ib.pos == ib.size) {
            size_t rd = fread(ibuf, 1, sizeof(ibuf), in);
            if (rd == 0) {
                ZSTD_freeDCtx(ctx);
                return -EILSEQ; /* 截断 */
            }
            ib.pos = 0;
            ib.size = rd;
        }
        ZSTD_outBuffer ob = {obuf, sizeof(obuf), 0};
        size_t rc = ZSTD_decompressStream(ctx, &ob, &ib);
        if (ZSTD_isError(rc)) {
            ZSTD_freeDCtx(ctx);
            return -EILSEQ;
        }
        if (ob.pos) {
            if (fwrite(obuf, 1, ob.pos, out) != ob.pos) {
                ZSTD_freeDCtx(ctx);
                return -EIO;
            }
            *n_out += ob.pos;
        }
        if (rc == 0) {
            ZSTD_freeDCtx(ctx);
            return 0;
        }
    }
}

/* ============================================================
 * 文件压缩/解压统一入口
 * ============================================================ */
static int impl_file(int compress_dir, const hw_compress_file_req_t *req,
                     hw_compress_file_resp_t *resp) {
    if (!req || !resp || !req->input_path || !req->output_path) return -EINVAL;
    FILE *in = fopen(req->input_path, "rb");
    if (!in) return -errno;
    FILE *out = fopen(req->output_path, "wb");
    if (!out) {
        int e = -errno;
        fclose(in);
        return e;
    }

    fseek(in, 0, SEEK_END);
    uint64_t in_len = (uint64_t)ftell(in);
    fseek(in, 0, SEEK_SET);

    uint64_t n_out = 0;
    int rc;
    int algo = req->algo;
    if (compress_dir) {
        if (algo == HW_COMPRESS_ALGO_AUTO) algo = HW_COMPRESS_ALGO_ZSTD;
        if (algo == HW_COMPRESS_ALGO_ZLIB || algo == HW_COMPRESS_ALGO_GZIP)
            rc = file_compress_zlib(in, out, algo == HW_COMPRESS_ALGO_GZIP, req->level, &n_out);
        else if (algo == HW_COMPRESS_ALGO_ZSTD)
            rc = file_compress_zstd(in, out, req->level, &n_out);
        else
            rc = -ENOTSUP;
    } else {
        uint8_t head[16];
        size_t hd = fread(head, 1, sizeof(head), in);
        fseek(in, 0, SEEK_SET);
        int fa = algo;
        if (fa == HW_COMPRESS_ALGO_AUTO) {
            rc = compress_detect(head, (uint32_t)hd, &fa);
            if (rc != 0) {
                fclose(in);
                fclose(out);
                return rc;
            }
        }
        algo = fa;
        if (fa == HW_COMPRESS_ALGO_ZLIB || fa == HW_COMPRESS_ALGO_GZIP)
            rc = file_decompress_zlib(in, out, &n_out);
        else if (fa == HW_COMPRESS_ALGO_ZSTD)
            rc = file_decompress_zstd(in, out, &n_out);
        else
            rc = -ENOTSUP;
    }

    fclose(in);
    if (rc == 0 && fclose(out) != 0) rc = -EIO;
    if (rc != 0) {
        remove(req->output_path); /* 失败不留残件 */
        return rc;
    }
    if (!req->keep_original) remove(req->input_path);
    if (compress_dir) {
        resp->orig_len = in_len;  /* 明文总长 */
        resp->result_len = n_out; /* 压缩产物字节 */
    } else {
        resp->orig_len = n_out;    /* 解出的明文总长 */
        resp->result_len = in_len; /* 消费的压缩帧字节 */
    }
    resp->algo = algo;
    return 0;
}

/* ============================================================
 * ops 表
 * ============================================================ */
static int32_t compress_version(void) {
    return 1;
}

static int ops_list_algorithms(hw_compress_info_t *out, int cap, int *count) {
    if (!out || !count || cap <= 0) return -EINVAL;
    static const hw_compress_info_t table[] = {
        {HW_COMPRESS_ALGO_ZLIB, "zlib", 1, 1, "78 01/9C/DA"},
        {HW_COMPRESS_ALGO_GZIP, "gzip", 1, 1, "1F 8B"},
        {HW_COMPRESS_ALGO_ZSTD, "zstd", 1, 1, "28 B5 2F FD"},
        {HW_COMPRESS_ALGO_XZ, "xz", 0, 1, "FD 37 7A 58"},
        {HW_COMPRESS_ALGO_LZ4, "lz4", 0, 1, "04 22 4D 18"},
        {HW_COMPRESS_ALGO_LZ4_RAW, "lz4-raw", 0, 0, "-"},
    };
    int n = (int)(sizeof(table) / sizeof(table[0]));
    int k = n < cap ? n : cap;
    memcpy(out, table, sizeof(table[0]) * (size_t)k);
    *count = n;
    return 0;
}

static int ops_compress_file(const hw_compress_file_req_t *req, hw_compress_file_resp_t *resp) {
    return impl_file(1, req, resp);
}

static int ops_decompress_file(const hw_compress_file_req_t *req, hw_compress_file_resp_t *resp) {
    return impl_file(0, req, resp);
}

static void ops_free_result(hw_compress_result_t *res) {
    if (!res) return;
    free(res->data);
    memset(res, 0, sizeof(*res));
}

hw_compress_ops_t hw_compress_ops = {
    .version = compress_version,
    .list_algorithms = ops_list_algorithms,
    .detect = compress_detect,
    .compress = compress_buffer,
    .decompress = compress_decompress,
    .compress_file = ops_compress_file,
    .decompress_file = ops_decompress_file,
    .free_result = ops_free_result,
};

#ifdef __cplusplus
}
#endif
