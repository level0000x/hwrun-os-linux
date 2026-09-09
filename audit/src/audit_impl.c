/*
 * audit_impl.c — AUDIT 协议实现（真实用户态事件审计）
 *
 * 存储模型：
 *   - 只追加文本日志 <dir>/audit.log（每行一条事件，格式见 audit.h）；
 *   - 容量控制按"单文件字节上限 max_bytes"，超出时自动滚动：
 *     audit.log → audit.log.1 → audit.log.2 ...，仅保留 max_files 个段，
 *     最旧段丢弃、保留最新——与设计稿 max_size/max_files 语义一致；
 *   - 事件 seq 在 store 内单调递增（跨滚动段、跨进程重启连续：首次使用前
 *     读取最新日志段末尾回填）。
 *
 * 错误约定：成功返回 0；失败返回负 errno。
 * 线程安全：进程内单实例全局 store（g_ctx），append/query/export/configure
 * 全部持同一把互斥锁，滚动与读取互斥。
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "audit.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 供 audit_core.c 生命周期（configure 分发 / start 参数注入）调用的内部参数入口。
 * -Wmissing-prototypes：全局函数定义前给出声明。 */
int hw_audit_configure(const char *key, const char *value);

#define HW_AUDIT_FILE "audit.log" /* 主日志文件名（滚动后缀 .1/.2/...） */
#define HW_AUDIT_LINE_MAX 4096    /* 单行缓冲（字段含转义放大，见 audit.h） */
#define HW_AUDIT_FILE_MODE 0644   /* 日志文件权限 */
#define HW_AUDIT_DIR_MODE 0755    /* 日志目录权限 */

/* ============================================================
 * store 上下文（进程内单实例）
 * ============================================================ */
typedef struct audit_store {
    char dir[HW_AUDIT_DIR_MAX]; /* 日志目录；空=尚未指定（首次使用取默认） */
    uint64_t max_bytes;         /* 单文件字节上限（触发滚动） */
    int max_files;              /* 保留段数（含主文件） */
    uint64_t next_seq;          /* 下一可用事件序号；0=未初始化（使用前回填） */
    pthread_mutex_t lock;
} audit_store_t;

static audit_store_t g_ctx = {
    .dir = {0},
    .max_bytes = HW_AUDIT_DEFAULT_MAX_BYTES,
    .max_files = HW_AUDIT_DEFAULT_MAX_FILES,
    .next_seq = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

/* ============================================================
 * 路径与字符串工具
 * ============================================================ */

/* 段路径：idx=0 → <dir>/audit.log；idx>=1 → <dir>/audit.log.<idx> */
static void seg_path(char *out, size_t cap, int idx) {
    if (idx <= 0)
        snprintf(out, cap, "%s/%s", g_ctx.dir, HW_AUDIT_FILE);
    else
        snprintf(out, cap, "%s/%s.%d", g_ctx.dir, HW_AUDIT_FILE, idx);
}

/* 字符串字段写入时转义：\\ | \n \r → 两字节序列（保证字段无裸 '|'/换行） */
static size_t fmt_esc(char *out, size_t cap, const char *s) {
    size_t o = 0;
    if (!s) s = "";
    while (*s) {
        const char *two = NULL;
        switch (*s) {
        case '\\':
            two = "\\\\";
            break;
        case '|':
            two = "\\|";
            break;
        case '\n':
            two = "\\n";
            break;
        case '\r':
            two = "\\r";
            break;
        default:
            break;
        }
        if (two) {
            if (o + 2 >= cap) break; /* 截断防御：正常输入（已按字段上限）不会触达 */
            out[o++] = two[0];
            out[o++] = two[1];
        } else {
            if (o + 1 >= cap) break;
            out[o++] = *s;
        }
        s++;
    }
    out[o] = '\0';
    return o;
}

/* 生成一行审计日志（含末尾 '\n'）。返回行长；缓冲不足返回 0（不应发生）。 */
static size_t fmt_line(const hw_audit_event_t *ev, char *buf, size_t cap) {
    char subj[HW_AUDIT_SUBJECT_MAX * 2 + 1];
    char act[HW_AUDIT_ACTION_MAX * 2 + 1];
    char tgt[HW_AUDIT_TARGET_MAX * 2 + 1];
    char det[HW_AUDIT_DETAIL_MAX * 2 + 1];
    fmt_esc(subj, sizeof(subj), ev->subject);
    fmt_esc(act, sizeof(act), ev->action);
    fmt_esc(tgt, sizeof(tgt), ev->target);
    fmt_esc(det, sizeof(det), ev->detail);

    int n = snprintf(buf, cap, "%llu|%lld|%s|%s|%s|%d|%s\n", (unsigned long long)ev->seq,
                     (long long)ev->ts, subj, act, tgt, (int)ev->result, det);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

/* 读取一个转义字段（'|' 分隔）。成功返回 0；*pos 越过分隔符。 */
static int esc_field(const char *line, size_t len, size_t *pos, char *out, size_t cap) {
    size_t o = 0;
    size_t k = *pos;
    while (k < len && line[k] != '|') {
        char c = line[k++];
        if (c == '\\') {
            if (k >= len) return -EILSEQ; /* 行尾孤立反斜杠 */
            char e = line[k++];
            if (e == '\\')
                c = '\\';
            else if (e == '|')
                c = '|';
            else if (e == 'n')
                c = '\n';
            else if (e == 'r')
                c = '\r';
            else
                return -EILSEQ; /* 未知转义（异源/损坏行） */
        }
        if (o + 1 >= cap) return -EILSEQ; /* 字段超长 */
        out[o++] = c;
    }
    out[o] = '\0';
    *pos = (k < len) ? k + 1 : k;
    return 0;
}

/* 解析一行日志为事件。损坏/异源行返回负 errno（调用方跳过该行）。 */
static int parse_event(const char *line, hw_audit_event_t *ev) {
    size_t len = strlen(line);
    while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        len--;
    if (len == 0) return -EILSEQ;

    memset(ev, 0, sizeof(*ev));
    size_t pos = 0;
    char num[32];
    char *end = NULL;

    /* seq */
    if (esc_field(line, len, &pos, num, sizeof(num)) != 0) return -EILSEQ;
    unsigned long long seq = strtoull(num, &end, 10);
    if (!end || *end || seq == 0) return -EILSEQ;
    ev->seq = seq;

    /* ts */
    if (esc_field(line, len, &pos, num, sizeof(num)) != 0) return -EILSEQ;
    long long ts = strtoll(num, &end, 10);
    if (end == num || *end) return -EILSEQ;
    ev->ts = ts;

    /* subject / action / target（字符串字段，行序见 audit.h） */
    if (esc_field(line, len, &pos, ev->subject, sizeof(ev->subject)) != 0) return -EILSEQ;
    if (esc_field(line, len, &pos, ev->action, sizeof(ev->action)) != 0) return -EILSEQ;
    if (esc_field(line, len, &pos, ev->target, sizeof(ev->target)) != 0) return -EILSEQ;

    /* result（数值字段，位于 detail 之前） */
    if (esc_field(line, len, &pos, num, sizeof(num)) != 0) return -EILSEQ;
    long r = strtol(num, &end, 10);
    if (end == num || *end || r < INT32_MIN || r > INT32_MAX) return -EILSEQ;
    ev->result = (int32_t)r;

    /* detail（最后一个字段，可为空） */
    if (esc_field(line, len, &pos, ev->detail, sizeof(ev->detail)) != 0) return -EILSEQ;

    if (pos != len) return -EILSEQ; /* 字段数多于 7（行结构异常） */
    return 0;
}

/* 查询条件匹配（空/0 表示不限，多条件 AND） */
static int event_matches(const hw_audit_event_t *ev, const hw_audit_query_t *q) {
    if (!q) return 1;
    if (q->start_ts > 0 && ev->ts < q->start_ts) return 0;
    if (q->end_ts > 0 && ev->ts > q->end_ts) return 0;
    if (q->subject[0] && strcmp(ev->subject, q->subject) != 0) return 0;
    if (q->action[0] && strcmp(ev->action, q->action) != 0) return 0;
    if (q->result_set && ev->result != q->result) return 0;
    return 1;
}

/* ============================================================
 * store 准备（目录缺省 / 序号回填）
 * ============================================================ */

static void set_default_dir_locked(void) {
    if (g_ctx.dir[0] == '\0') snprintf(g_ctx.dir, sizeof(g_ctx.dir), "%s", HW_AUDIT_DEFAULT_DIR);
}

static int ensure_dir_locked(void) {
    set_default_dir_locked();
    if (mkdir(g_ctx.dir, HW_AUDIT_DIR_MODE) != 0 && errno != EEXIST) return -errno;
    return 0;
}

/* 首次使用前回填 next_seq：找最新一个非空日志段（idx 0=base 最新），解析其
 * 最后一条合法事件，next_seq=seq+1；保证跨进程重启序号连续、不重复。 */
static void seed_seq_locked(void) {
    if (g_ctx.next_seq != 0) return;
    g_ctx.next_seq = 1;
    char path[HW_AUDIT_DIR_MAX + 128];
    for (int idx = 0; idx < g_ctx.max_files; idx++) {
        seg_path(path, sizeof(path), idx);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        struct stat st;
        if (fstat(fileno(f), &st) != 0 || st.st_size == 0) {
            fclose(f);
            continue;
        }
        char line[HW_AUDIT_LINE_MAX];
        uint64_t last = 0;
        while (fgets(line, sizeof(line), f)) {
            hw_audit_event_t ev;
            if (parse_event(line, &ev) == 0 && ev.seq > last) last = ev.seq;
        }
        fclose(f);
        if (last > 0) g_ctx.next_seq = last + 1;
        return; /* 首个非空段即最新（idx 升序） */
    }
}

/* ============================================================
 * 滚动（容量控制）：主文件 → .1 → .2 ...，保留 max_files 个段
 * ============================================================ */
static int rotate_locked(void) {
    char src[HW_AUDIT_DIR_MAX + 128];
    char dst[HW_AUDIT_DIR_MAX + 128];

    if (g_ctx.max_files <= 1) {
        /* 只保留主文件：直接截断（旧事件丢弃，保留最新段） */
        seg_path(src, sizeof(src), 0);
        if (truncate(src, 0) != 0 && errno != ENOENT) return -errno;
        return 0;
    }

    /* 高位段依次上移：.(max_files-2)→.(max_files-1) ... .0(base)→.1 */
    for (int i = g_ctx.max_files - 1; i >= 1; i--) {
        seg_path(src, sizeof(src), i - 1);
        seg_path(dst, sizeof(dst), i);
        if (rename(src, dst) != 0 && errno != ENOENT) return -errno;
    }
    /* 清理配置调小后可能残留的超额旧段 */
    for (int i = g_ctx.max_files; i < HW_AUDIT_SEG_MAX; i++) {
        seg_path(src, sizeof(src), i);
        if (unlink(src) != 0 && errno != ENOENT) return -errno;
    }
    return 0;
}

/* ============================================================
 * 读取收集：全部保留段（旧→新）扫描，按条件收集事件（调用方须持锁）
 * ============================================================ */
static int collect_locked(const hw_audit_query_t *q, hw_audit_event_t **out, int *out_count) {
    hw_audit_event_t *arr = NULL;
    int cap = 0;
    int n = 0;
    char path[HW_AUDIT_DIR_MAX + 128];

    for (int idx = g_ctx.max_files - 1; idx >= 0; idx--) {
        seg_path(path, sizeof(path), idx);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char line[HW_AUDIT_LINE_MAX];
        while (fgets(line, sizeof(line), f)) {
            hw_audit_event_t ev;
            if (parse_event(line, &ev) != 0) continue; /* 损坏/半行：跳过 */
            if (!event_matches(&ev, q)) continue;
            if (n == cap) {
                int ncap = cap ? cap * 2 : 64;
                hw_audit_event_t *na =
                    (hw_audit_event_t *)realloc(arr, sizeof(hw_audit_event_t) * (size_t)ncap);
                if (!na) {
                    free(arr);
                    fclose(f);
                    *out = NULL;
                    *out_count = 0;
                    return -ENOMEM;
                }
                arr = na;
                cap = ncap;
            }
            arr[n++] = ev;
        }
        fclose(f);
    }

    /* 排序与上限：先升序收集，DESC 则整体反转；limit 截断保留最前 */
    if (q && q->order == HW_AUDIT_ORDER_DESC) {
        for (int i = 0, j = n - 1; i < j; i++, j--) {
            hw_audit_event_t t = arr[i];
            arr[i] = arr[j];
            arr[j] = t;
        }
    }
    if (q && q->limit > 0 && n > q->limit) n = q->limit;

    *out = arr; /* 无匹配时可能为 NULL */
    *out_count = n;
    return 0;
}

/* ============================================================
 * ops 实现
 * ============================================================ */
static int32_t ops_version(void) {
    return 1;
}

static int ops_append(const hw_audit_event_t *ev, uint64_t *out_seq) {
    if (!ev || !out_seq) return -EINVAL;
    if (!ev->subject[0] || !ev->action[0]) return -EINVAL; /* 谁/做了什么 必填 */

    /* 本地副本：保证 NUL 结尾与字段上限截断，并规范化时间戳 */
    hw_audit_event_t e;
    memset(&e, 0, sizeof(e));
    snprintf(e.subject, sizeof(e.subject), "%s", ev->subject);
    snprintf(e.action, sizeof(e.action), "%s", ev->action);
    snprintf(e.target, sizeof(e.target), "%s", ev->target ? ev->target : "");
    snprintf(e.detail, sizeof(e.detail), "%s", ev->detail ? ev->detail : "");
    e.result = ev->result;
    e.ts = ev->ts > 0 ? ev->ts : (int64_t)time(NULL);

    pthread_mutex_lock(&g_ctx.lock);
    int rc = ensure_dir_locked();
    if (rc == 0) {
        seed_seq_locked();
        e.seq = g_ctx.next_seq;

        char line[HW_AUDIT_LINE_MAX];
        size_t linelen = fmt_line(&e, line, sizeof(line));
        if (linelen == 0) {
            rc = -EILSEQ;
        } else {
            char path[HW_AUDIT_DIR_MAX + 128];
            seg_path(path, sizeof(path), 0);
            FILE *f = fopen(path, "a");
            if (!f) {
                rc = -errno;
            } else {
                struct stat st;
                if (fstat(fileno(f), &st) != 0) {
                    rc = -errno;
                } else if (st.st_size > 0 && (uint64_t)st.st_size + linelen > g_ctx.max_bytes) {
                    /* 触发滚动：换新主文件后写入本事件 */
                    fclose(f);
                    f = NULL;
                    rc = rotate_locked();
                    if (rc == 0) {
                        f = fopen(path, "a");
                        if (!f) rc = -errno;
                    }
                }
                if (rc == 0) {
                    if (fwrite(line, 1, linelen, f) != linelen) {
                        rc = -EIO;
                    } else {
                        g_ctx.next_seq++;
                        *out_seq = e.seq;
                    }
                }
                if (f) fclose(f);
            }
        }
    }
    pthread_mutex_unlock(&g_ctx.lock);
    return rc;
}

static int ops_query(const hw_audit_query_t *q, hw_audit_event_t **out_events, int *out_count) {
    if (!out_events || !out_count) return -EINVAL;
    pthread_mutex_lock(&g_ctx.lock);
    set_default_dir_locked();
    int rc = collect_locked(q, out_events, out_count);
    pthread_mutex_unlock(&g_ctx.lock);
    return rc;
}

static void ops_events_free(hw_audit_event_t *events, int count) {
    (void)count;
    free(events);
}

static int ops_export_events(const hw_audit_query_t *q, const char *path, uint64_t *out_count) {
    if (!path) return -EINVAL;
    pthread_mutex_lock(&g_ctx.lock);
    set_default_dir_locked();

    hw_audit_event_t *arr = NULL;
    int n = 0;
    int rc = collect_locked(q, &arr, &n);
    uint64_t written = 0;
    if (rc == 0) {
        FILE *f = fopen(path, "w");
        if (!f) {
            rc = -errno;
        } else {
            char line[HW_AUDIT_LINE_MAX];
            for (int i = 0; i < n; i++) {
                size_t linelen = fmt_line(&arr[i], line, sizeof(line));
                if (linelen == 0) {
                    rc = -EILSEQ;
                    break;
                }
                if (fwrite(line, 1, linelen, f) != linelen) {
                    rc = -EIO;
                    break;
                }
                written++;
            }
            if (fclose(f) != 0 && rc == 0) rc = -EIO;
            if (rc != 0) remove(path); /* 失败不留残件 */
        }
    }
    free(arr);
    pthread_mutex_unlock(&g_ctx.lock);
    if (out_count) *out_count = written;
    return rc;
}

static int ops_status(hw_audit_status_t *st) {
    if (!st) return -EINVAL;
    pthread_mutex_lock(&g_ctx.lock);
    set_default_dir_locked();
    seed_seq_locked();
    memset(st, 0, sizeof(*st));
    snprintf(st->dir, sizeof(st->dir), "%s", g_ctx.dir);
    st->seq_next = g_ctx.next_seq;
    st->total_events = g_ctx.next_seq > 0 ? g_ctx.next_seq - 1 : 0;

    char path[HW_AUDIT_DIR_MAX + 128];
    for (int idx = 0; idx < g_ctx.max_files; idx++) {
        seg_path(path, sizeof(path), idx);
        struct stat stt;
        if (stat(path, &stt) == 0 && S_ISREG(stt.st_mode)) {
            st->segments++;
            st->total_bytes += (uint64_t)stt.st_size;
        }
    }
    pthread_mutex_unlock(&g_ctx.lock);
    return 0;
}

/* ============================================================
 * 参数工具与参数入口（configure 回调 / start 注入共用）
 * ============================================================ */

/* "1048576" / "1MB" / "64k" / "256KB" 等形式 → 字节数 */
static int parse_size(const char *s, uint64_t *out) {
    if (!s || !*s) return -EINVAL;
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || v == 0) return -EINVAL;
    uint64_t mult = 1;
    if (*end) {
        char u = *end;
        if (u == 'k' || u == 'K')
            mult = 1024ULL;
        else if (u == 'm' || u == 'M')
            mult = 1024ULL * 1024;
        else if (u == 'g' || u == 'G')
            mult = 1024ULL * 1024 * 1024;
        else
            return -EINVAL;
        end++;
        if (*end == 'b' || *end == 'B') end++;
    }
    if (*end != '\0') return -EINVAL;
    if (v > UINT64_MAX / mult) return -EINVAL;
    *out = v * mult;
    return 0;
}

static int parse_int_range(const char *s, int *out, int lo, int hi) {
    if (!s || !*s) return -EINVAL;
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < lo || v > hi) return -EINVAL;
    *out = (int)v;
    return 0;
}

int hw_audit_configure(const char *key, const char *value) {
    if (!key) return -EINVAL;
    if (!value) return 0; /* 未知/无值：接受，不改变现有配置 */

    pthread_mutex_lock(&g_ctx.lock);
    if (strcmp(key, HW_AUDIT_PARAM_PATH) == 0) {
        if (!value[0]) {
            pthread_mutex_unlock(&g_ctx.lock);
            return 0;
        }
        char norm[HW_AUDIT_DIR_MAX];
        snprintf(norm, sizeof(norm), "%s", value);
        size_t l = strlen(norm);
        while (l > 1 && norm[l - 1] == '/')
            norm[--l] = '\0'; /* 去尾部 '/' */
        if (strcmp(norm, g_ctx.dir) == 0) {
            pthread_mutex_unlock(&g_ctx.lock);
            return 0; /* 目录未变：不打断序号 */
        }
        snprintf(g_ctx.dir, sizeof(g_ctx.dir), "%s", norm);
        g_ctx.next_seq = 0; /* 新目录：首次使用前按文件回填 */
        pthread_mutex_unlock(&g_ctx.lock);
        return 0;
    }
    if (strcmp(key, HW_AUDIT_PARAM_MAX_BYTES) == 0) {
        uint64_t b;
        int rc = parse_size(value, &b);
        if (rc == 0) g_ctx.max_bytes = b;
        pthread_mutex_unlock(&g_ctx.lock);
        return rc;
    }
    if (strcmp(key, HW_AUDIT_PARAM_MAX_FILES) == 0) {
        int n;
        int rc = parse_int_range(value, &n, 1, HW_AUDIT_SEG_MAX);
        if (rc == 0) g_ctx.max_files = n;
        pthread_mutex_unlock(&g_ctx.lock);
        return rc;
    }
    pthread_mutex_unlock(&g_ctx.lock);
    return 0; /* 未知键：接受（本插件无此配置项） */
}

/* ============================================================
 * ops 表（audit_core.c 经 get_interface 对外暴露）
 * ============================================================ */
hw_audit_ops_t hw_audit_ops = {
    .version = ops_version,
    .append = ops_append,
    .query = ops_query,
    .events_free = ops_events_free,
    .export_events = ops_export_events,
    .status = ops_status,
};

#ifdef __cplusplus
}
#endif
