/*
 * compress.h — HWRun OS COMPRESS 协议插件公共接口
 *
 * COMPRESS (Compression Protocol) 是压缩服务协议，提供缓冲/文件压缩、
 * 解压与格式自动识别的统一抽象，面向日志、传输、存储与归档场景。
 * 依据根目录设计稿《HWRun OS COMPRE.txt》（状态：设计冻结）落地。
 *
 * 依赖链：METAPROTO → BUS → PARAM → LOG → COMPRESS
 * requires: LOG / PARAM / METAPROTO
 *
 * 实现方案：真实调用系统压缩库——zlib（gzip/zlib 格式）+ libzstd（zstd frame）。
 * xz/lz4 在 v1 未链接（返回 -ENOTSUP），仅出现在算法能力表中供探测与扩展。
 * 绝无假数据/占位。
 *
 * 错误约定：成功返回 HWRUN_OK(0)；失败返回负 errno（-EINVAL/-ENOMEM/-ENOTSUP/
 * -EILSEQ/-EIO）。解压数据由实现分配，调用方用 ops.free_result() 释放。
 */

#ifndef HWRUN_COMPRESS_H
#define HWRUN_COMPRESS_H

#include "hwrun.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 压缩算法标识（与设计稿 compress_algo_t 对齐）
 * ============================================================ */
typedef enum {
    HW_COMPRESS_ALGO_AUTO = 0, /* 自动识别(解压) / 默认(压缩) */
    HW_COMPRESS_ALGO_ZLIB,     /* deflate (zlib 容器) */
    HW_COMPRESS_ALGO_GZIP,     /* gzip 格式 */
    HW_COMPRESS_ALGO_ZSTD,     /* zstd frame */
    HW_COMPRESS_ALGO_XZ,       /* xz（v1 仅探测，未链接） */
    HW_COMPRESS_ALGO_LZ4,      /* lz4 frame（v1 仅探测，未链接） */
    HW_COMPRESS_ALGO_LZ4_RAW,  /* lz4 block（v1 仅探测，未链接） */
} hw_compress_algo_t;

/* ============================================================
 * 压缩级别标识（设计稿 compress_level_t）
 * ============================================================ */
typedef enum {
    HW_COMPRESS_LEVEL_FASTEST = 0, /* 最快 */
    HW_COMPRESS_LEVEL_FAST,        /* 快 */
    HW_COMPRESS_LEVEL_DEFAULT,     /* 默认(平衡) */
    HW_COMPRESS_LEVEL_MORE,        /* 更高 */
    HW_COMPRESS_LEVEL_BEST,        /* 最高 */
} hw_compress_level_t;

/* 算法能力表项 */
typedef struct hw_compress_info {
    int algo;          /* hw_compress_algo_t */
    const char *name;  /* "zlib"/"gzip"/"zstd"/... */
    int supported;     /* 1=已链接可用; 0=仅探测/待扩展 */
    int can_detect;    /* 1=可被 compress.detect 识别 */
    const char *magic; /* 魔数描述，如 "1F 8B" */
} hw_compress_info_t;

/* ============================================================
 * 缓冲压缩/解压结果（data 由实现 malloc，调用方 free_result 释放）
 * ============================================================ */
typedef struct hw_compress_result {
    uint8_t *data; /* 压缩后数据 / 解压后数据（malloc 缓冲） */
    uint32_t data_len;
    uint32_t orig_len; /* 压缩输入原长 / 解压前原始帧的原长（探测可得则填） */
    int algo;          /* 实际算法：压缩=请求算法；解压=识别到的算法 */
} hw_compress_result_t;

/* ============================================================
 * 文件压缩/解压请求/响应
 * ============================================================ */
typedef struct hw_compress_file_req {
    const char *input_path;
    const char *output_path;
    int algo;          /* 压缩: 指定算法; 解压: 0=自动识别 */
    int level;         /* 压缩级别（解压忽略） */
    int keep_original; /* 0=成功后删除原文件 */
} hw_compress_file_req_t;

typedef struct hw_compress_file_resp {
    uint64_t orig_len;
    uint64_t result_len;
    int algo; /* 解压时实际识别算法 */
} hw_compress_file_resp_t;

/* ============================================================
 * COMPRESS 协议接口 (get_interface("COMPRESS") 返回此指针)
 * ============================================================ */
typedef struct hw_compress_ops {
    int32_t (*version)(void);

    /* 算法能力表 */
    int (*list_algorithms)(hw_compress_info_t *out, int cap, int *count);

    /* 格式识别：仅看魔数，返回 hw_compress_algo_t；不认识返回 AUTO */
    int (*detect)(const uint8_t *data, uint32_t data_len, int *algo);

    /* 缓冲压缩/解压（force_algo=0 时自动识别） */
    int (*compress)(int algo, int level, const uint8_t *in, uint32_t in_len,
                    hw_compress_result_t *out);
    int (*decompress)(int force_algo, const uint8_t *in, uint32_t in_len,
                      hw_compress_result_t *out);

    /* 文件压缩/解压（流式，路径级操作） */
    int (*compress_file)(const hw_compress_file_req_t *req, hw_compress_file_resp_t *resp);
    int (*decompress_file)(const hw_compress_file_req_t *req, hw_compress_file_resp_t *resp);

    /* 释放 result.data */
    void (*free_result)(hw_compress_result_t *res);
} hw_compress_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_COMPRESS_H */
