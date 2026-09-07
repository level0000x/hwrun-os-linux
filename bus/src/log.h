/*
 * log.h — LOG 日志协议接口
 *
 * 系统统一日志。依赖链第 1 环。
 */

#ifndef HWRUN_LOG_H
#define HWRUN_LOG_H

#include "hwrun.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 日志级别 */
enum {
    HWLOG_DEBUG   = 0,
    HWLOG_INFO    = 1,
    HWLOG_WARN    = 2,
    HWLOG_ERROR   = 3,
    HWLOG_FATAL   = 4,
};

struct hw_log_ctx;

typedef struct hw_log_ops {
    void (*log)(int level, const char *plugin_id,
                const char *fmt, va_list ap);
    void (*set_level)(int level);
    int  (*get_level)(void);
} hw_log_ops_t;

/* 日志实现入口 */
typedef struct hw_log_ctx {
    int   level;              /* 当前日志级别 */
    FILE *out;                /* 输出流       */
    char  output_path[256];
    int   to_file;
    int   with_timestamp;
    int   with_color;
} hw_log_ctx_t;

extern int  hw_log_init(hw_log_ctx_t *ctx, const char *path, int level);
extern void hw_log_shutdown(hw_log_ctx_t *ctx);
extern void hw_log_write(hw_log_ctx_t *ctx, int level, const char *plugin_id,
                         const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 4, 5)))
#endif
    ;

#define HWLOG_DEBUGF(ctx, plug, ...) hw_log_write((ctx), HWLOG_DEBUG, (plug), __VA_ARGS__)
#define HWLOG_INFOF(ctx, plug, ...)  hw_log_write((ctx), HWLOG_INFO,  (plug), __VA_ARGS__)
#define HWLOG_WARNF(ctx, plug, ...)  hw_log_write((ctx), HWLOG_WARN,  (plug), __VA_ARGS__)
#define HWLOG_ERRF(ctx, plug, ...)   hw_log_write((ctx), HWLOG_ERROR, (plug), __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_LOG_H */