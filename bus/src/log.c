/*
 * log.c — LOG 日志完整实现
 *
 * 依赖链第 1 环。统一日志输出到控制台/文件，含级别、时间戳、颜色。
 */

#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static const char *level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};
static const char *level_colors[] = {
    "\x1b[90m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m"
};
#define COLOR_RESET "\x1b[0m"

int hw_log_init(hw_log_ctx_t *ctx, const char *path, int level) {
    if (!ctx) return HWRUN_EINVAL;
    memset(ctx, 0, sizeof(*ctx));
    ctx->level = level;
    ctx->out = stdout;
    ctx->with_timestamp = 1;
    ctx->with_color = 1;
    if (path && *path) {
        FILE *fp = fopen(path, "a");
        if (!fp) return HWRUN_ENOENT;
        snprintf(ctx->output_path, sizeof(ctx->output_path), "%s", path);
        ctx->out = fp;
        ctx->to_file = 1;
        ctx->with_color = 0;    /* 文件不写 ANSI 颜色 */
    }
    return HWRUN_OK;
}

void hw_log_shutdown(hw_log_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->to_file && ctx->out) fclose(ctx->out);
    ctx->out = NULL;
}

void hw_log_write(hw_log_ctx_t *ctx, int level, const char *plugin_id,
                  const char *fmt, ...) {
    if (!ctx || !ctx->out) return;
    if (level < ctx->level) return;
    if (level < HWLOG_DEBUG) level = HWLOG_DEBUG;
    if (level > HWLOG_FATAL) level = HWLOG_FATAL;

    va_list ap;
    char msg[2048];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char ts[32] = "";
    if (ctx->with_timestamp) {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
    }

    FILE *out = ctx->out;
    if (ctx->with_color) {
        fprintf(out, "%s[%s][%-5s]%s %s %s\n",
                level_colors[level], ts, level_names[level], COLOR_RESET,
                plugin_id ? plugin_id : "", msg);
    } else {
        if (*ts)
            fprintf(out, "[%s][%-5s] %s %s\n", ts, level_names[level],
                    plugin_id ? plugin_id : "", msg);
        else
            fprintf(out, "[%-5s] %s %s\n", level_names[level],
                    plugin_id ? plugin_id : "", msg);
    }
    fflush(out);
}