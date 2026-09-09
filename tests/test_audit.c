/*
 * test_audit.c — CMocka 单元测试：AUDIT 用户态事件审计（dlopen .so 后真实调用）
 *
 * 测试对象：audit/build/audit.so 插件及其 provides 协议 "AUDIT" 的
 * hw_audit_ops_t（真实文件审计，非打桩）。AUDIT 的 requires 含 LOG/PARAM/
 * METAPROTO，但实现自包含（仅 libc+pthread），故单独 dlopen 亦可真跑。
 *
 * 链路（与 test_compress.c 一致）：
 *   dlopen(.so) -> hw_plugin_entry() -> init()
 *   -> metaproto_register(provides, implementation=get_interface("AUDIT"))
 *   -> start -> metaproto_resolve("AUDIT") -> hw_audit_ops_t*
 *
 * 每个用例在 /tmp 下 mkdtemp 独立审计目录（经插件 configure 注入
 * "audit.log.path"），互不污染；teardown 清理文件与目录。
 *
 * 覆盖要点（全部真实读写）：
 *   - append 若干事件 + 日志文件存在非空可读；
 *   - query 过滤：主体/动作/结果/时间区间/组合，升序与 DESC+limit；
 *   - 无匹配查询：返回 0 条、events 为 NULL；
 *   - 导出文件与审计日志内容一致（字节相等），支持条件导出；
 *   - 容量滚动：max_bytes 触发滚动、仅保留 max_files 段、旧事件丢弃、
 *     最新事件仍在、seq 连续（status）。
 *
 * 错误约定：成功返回 0，失败返回负 errno。query 返回数组用 ops.events_free 释放。
 * 清理：stop -> unregister -> destroy -> dlclose；p 是 .so 静态描述符不得 free。
 */

#include "hwrun.h"
#include "bus.h"
#include "metaproto.h"
#include "../audit/include/audit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <setjmp.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <cmocka.h>

#ifndef HWRUN_PLUGIN_ROOT
#define HWRUN_PLUGIN_ROOT "../"
#endif

static hw_bus_t g_bus;
static void *g_h = NULL; /* dlopen 句柄 */
static hw_plugin_t *g_p = NULL;
static hw_audit_ops_t *g_ops = NULL;

/* ---- 组级 setup：总线 + dlopen + 生命周期 + 注册 provides ---- */
static int group_setup(void **state) {
    (void)state;
    if (hw_bus_init(&g_bus, NULL, NULL, HWLOG_WARN) != HWRUN_OK) {
        fprintf(stderr, "test_audit: hw_bus_init 失败\n");
        return -1;
    }

    char so[512];
    snprintf(so, sizeof(so), HWRUN_PLUGIN_ROOT "audit/build/audit.so");
    g_h = dlopen(so, RTLD_NOW | RTLD_GLOBAL);
    if (!g_h) {
        printf("  [skip] dlopen(%s): %s\n", so, dlerror());
        return 0;
    }

    hw_plugin_t *(*entry)(void) = (hw_plugin_t * (*)(void)) dlsym(g_h, "hw_plugin_entry");
    if (!entry) {
        printf("  [skip] %s: 无 hw_plugin_entry\n", so);
        dlclose(g_h);
        g_h = NULL;
        return 0;
    }
    hw_plugin_t *self = entry();
    if (!self) {
        dlclose(g_h);
        g_h = NULL;
        return 0;
    }

    if (self->ops.init) self->ops.init(self);
    self->state = HWPLUGIN_LOADED;

    for (int i = 0; i < self->provides_count; i++) {
        void *impl = self->ops.get_interface ? self->ops.get_interface(self->provides[i]) : NULL;
        hw_metaproto_register(&g_bus.meta, self->provides[i], HWRUN_PROTOCOL_VERSION, self->id,
                              impl ? impl : (void *)self);
    }
    if (self->ops.start) self->ops.start(self);
    self->state = HWPLUGIN_STARTED;
    g_p = self;

    hw_protocol_route_t *r = NULL;
    if (hw_metaproto_resolve(&g_bus.meta, "AUDIT", NULL, &r) == HWRUN_OK && r && r->implementation)
        g_ops = (hw_audit_ops_t *)r->implementation;
    else
        printf("  [warn] resolve(AUDIT) 未取得实现指针\n");
    return 0;
}

static int group_teardown(void **state) {
    (void)state;
    if (!g_p) return 0;
    if (g_p->ops.stop) g_p->ops.stop(g_p);
    for (int i = 0; i < g_p->provides_count; i++)
        hw_metaproto_unregister(&g_bus.meta, g_p->provides[i], g_p->id);
    if (g_p->ops.destroy) g_p->ops.destroy(g_p);
    dlclose(g_h);
    g_h = NULL;
    g_p = NULL;
    g_ops = NULL;
    hw_bus_shutdown(&g_bus);
    return 0;
}

/* ---- 用例级 setup/teardown：每个用例独立 /tmp 审计目录 ---- */
typedef struct audit_ctx {
    char dir[320]; /* mkdtemp 临时审计目录（本用例专属） */
    char log[340]; /* <dir>/audit.log */
    char exp[340]; /* <dir>/export.log */
} audit_ctx_t;

static int audit_test_setup(void **state) {
    if (!g_p || !g_p->ops.configure || !g_ops) return -1;
    audit_ctx_t *t = (audit_ctx_t *)calloc(1, sizeof(*t));
    assert_non_null(t);
    snprintf(t->dir, sizeof(t->dir), "/tmp/hwad_%ld_XXXXXX", (long)getpid());
    if (!mkdtemp(t->dir)) {
        free(t);
        return -1;
    }
    snprintf(t->log, sizeof(t->log), "%s/audit.log", t->dir);
    snprintf(t->exp, sizeof(t->exp), "%s/export.log", t->dir);

    /* 注入参数风格配置：独立目录 + 恢复默认容量（各用例互不依赖） */
    assert_int_equal(g_p->ops.configure(g_p, HW_AUDIT_PARAM_PATH, t->dir), 0);
    assert_int_equal(g_p->ops.configure(g_p, HW_AUDIT_PARAM_MAX_BYTES, "1MB"), 0);
    assert_int_equal(g_p->ops.configure(g_p, HW_AUDIT_PARAM_MAX_FILES, "3"), 0);
    *state = t;
    return 0;
}

static int audit_test_teardown(void **state) {
    audit_ctx_t *t = (audit_ctx_t *)*state;
    if (t) {
        for (int i = 0; i < 32; i++) {
            char p[380];
            if (i == 0)
                snprintf(p, sizeof(p), "%s/audit.log", t->dir);
            else
                snprintf(p, sizeof(p), "%s/audit.log.%d", t->dir, i);
            unlink(p);
        }
        for (int i = 0; i < 8; i++) {
            char p[380];
            snprintf(p, sizeof(p), "%s/export%d.log", t->dir, i);
            unlink(p);
        }
        unlink(t->exp);
        rmdir(t->dir);
        free(t);
    }
    return 0;
}

/* ---- 工具 ---- */

/* 追加一条事件，返回实现分配的事件 seq（首个为 1） */
static uint64_t append_ev(int64_t ts, const char *subj, const char *act, const char *tgt, int res,
                          const char *det) {
    hw_audit_event_t e;
    memset(&e, 0, sizeof(e));
    e.ts = ts;
    e.result = res;
    snprintf(e.subject, sizeof(e.subject), "%s", subj ? subj : "");
    snprintf(e.action, sizeof(e.action), "%s", act ? act : "");
    snprintf(e.target, sizeof(e.target), "%s", tgt ? tgt : "");
    snprintf(e.detail, sizeof(e.detail), "%s", det ? det : "");
    uint64_t seq = 0;
    assert_int_equal(g_ops->append(&e, &seq), 0);
    assert_true(seq >= 1);
    return seq;
}

/* 执行查询并返回命中条数；数组由调用方 events_free 释放 */
static int run_query(const hw_audit_query_t *q, hw_audit_event_t **evs) {
    int n = -1;
    assert_int_equal(g_ops->query(q, evs, &n), 0);
    assert_true(n >= 0);
    return n;
}

static void free_query(hw_audit_event_t *evs, int n) {
    g_ops->events_free(evs, n);
}

/* 读整个文件（供导出内容比对） */
static int read_file(const char *path, char **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    struct stat st;
    if (fstat(fileno(f), &st) != 0 || st.st_size < 0) {
        fclose(f);
        return -1;
    }
    size_t sz = (size_t)st.st_size;
    char *buf = (char *)malloc(sz ? sz : 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    *out_len = fread(buf, 1, sz, f);
    fclose(f);
    *out = buf;
    return 0;
}

/* ---- 用例 ---- */

/* append + 日志文件存在非空、行可读 */
static void test_audit_append_and_file(void **state) {
    audit_ctx_t *t = (audit_ctx_t *)*state;
    (void)t;
    if (!g_ops) {
        skip();
        return;
    }
    uint64_t s1 = append_ev(1000, "alice", "login", "console", 0, "auth ok");
    uint64_t s2 = append_ev(1000, "alice", "write", "/etc/app.conf", -EACCES, "permission denied");
    uint64_t s3 = append_ev(1000, "bob", "read", "/var/log/hwrun", 0, NULL);
    assert_int_equal(s1, 1);
    assert_int_equal(s2, 2);
    assert_int_equal(s3, 3);

    /* 日志文件存在且非空，行可读（fgets 能读到明文内容） */
    assert_int_equal(access(t->log, F_OK), 0);
    struct stat st;
    assert_int_equal(stat(t->log, &st), 0);
    assert_true(st.st_size > 0);

    FILE *f = fopen(t->log, "r");
    assert_non_null(f);
    char line[512];
    int lines = 0;
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        lines++;
        if (strstr(line, "alice") && strstr(line, "login") && strstr(line, "auth ok")) found = 1;
    }
    fclose(f);
    assert_int_equal(lines, 3);
    assert_int_equal(found, 1);
    printf("  [info] append: seq 1..3, log file %zu bytes, 3 readable lines\n", st.st_size);
}

/* 条件查询：主体/动作/结果/时间区间/组合 + 排序与上限 */
static void test_audit_query_filters(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    /* 1 alice login   ts=1000 res=0
       2 alice write   ts=2000 res=-EACCES
       3 bob   login   ts=3000 res=0
       4 bob   write   ts=4000 res=0
       5 carol read    ts=5000 res=0 */
    append_ev(1000, "alice", "login", "console", 0, "auth ok");
    append_ev(2000, "alice", "write", "/etc/app.conf", -EACCES, "permission denied");
    append_ev(3000, "bob", "login", "console", 0, "auth ok");
    append_ev(4000, "bob", "write", "/var/lib/hwrun/data", 0, "ok");
    append_ev(5000, "carol", "read", "/etc/passwd", 0, NULL);

    hw_audit_event_t *evs = NULL;
    int n = 0;

    /* 主体过滤 */
    hw_audit_query_t q;
    memset(&q, 0, sizeof(q));
    snprintf(q.subject, sizeof(q.subject), "%s", "alice");
    n = run_query(&q, &evs);
    assert_int_equal(n, 2);
    assert_int_equal(evs[0].seq, 1);
    assert_int_equal(evs[1].seq, 2);
    assert_int_equal(evs[0].result, 0);
    assert_int_equal(evs[1].result, -EACCES);
    assert_string_equal(evs[1].target, "/etc/app.conf");
    assert_string_equal(evs[1].detail, "permission denied");
    free_query(evs, n);

    /* 动作过滤 */
    memset(&q, 0, sizeof(q));
    snprintf(q.action, sizeof(q.action), "%s", "write");
    n = run_query(&q, &evs);
    assert_int_equal(n, 2);
    assert_int_equal(evs[0].seq, 2);
    assert_int_equal(evs[1].seq, 4);
    free_query(evs, n);

    /* 结果过滤：成功(0) 与失败(-EACCES) */
    memset(&q, 0, sizeof(q));
    q.result_set = 1;
    q.result = 0;
    n = run_query(&q, &evs);
    assert_int_equal(n, 4); /* seq 1,3,4,5 */
    free_query(evs, n);
    memset(&q, 0, sizeof(q));
    q.result_set = 1;
    q.result = -EACCES;
    n = run_query(&q, &evs);
    assert_int_equal(n, 1);
    assert_int_equal(evs[0].seq, 2);
    free_query(evs, n);

    /* 时间区间过滤 [3000, 4000] */
    memset(&q, 0, sizeof(q));
    q.start_ts = 3000;
    q.end_ts = 4000;
    n = run_query(&q, &evs);
    assert_int_equal(n, 2);
    assert_int_equal(evs[0].seq, 3);
    assert_int_equal(evs[1].seq, 4);
    free_query(evs, n);

    /* 组合过滤：主体 bob + 动作 write */
    memset(&q, 0, sizeof(q));
    snprintf(q.subject, sizeof(q.subject), "%s", "bob");
    snprintf(q.action, sizeof(q.action), "%s", "write");
    n = run_query(&q, &evs);
    assert_int_equal(n, 1);
    assert_int_equal(evs[0].seq, 4);
    free_query(evs, n);

    /* 降序 + 上限：最新 2 条为 seq 5,4 */
    memset(&q, 0, sizeof(q));
    q.order = HW_AUDIT_ORDER_DESC;
    q.limit = 2;
    n = run_query(&q, &evs);
    assert_int_equal(n, 2);
    assert_int_equal(evs[0].seq, 5);
    assert_int_equal(evs[1].seq, 4);
    free_query(evs, n);

    printf("  [info] query filters: subject/action/result/time/combined/desc-limit all pass\n");
}

/* 无匹配查询：0 条 + events 为 NULL */
static void test_audit_query_none(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    append_ev(1000, "alice", "login", "console", 0, "auth ok");
    append_ev(2000, "bob", "read", "/etc/hostname", 0, NULL);

    hw_audit_event_t dummy; /* 脏值来源：验证无匹配时实现会把 *out 置 NULL */
    hw_audit_event_t *evs = &dummy;
    hw_audit_query_t q;
    memset(&q, 0, sizeof(q));
    snprintf(q.subject, sizeof(q.subject), "%s", "ghost-user");
    int n = run_query(&q, &evs);
    assert_int_equal(n, 0);
    assert_null(evs);

    /* 不存在的失败码过滤 */
    evs = &dummy;
    memset(&q, 0, sizeof(q));
    q.result_set = 1;
    q.result = -12345;
    n = run_query(&q, &evs);
    assert_int_equal(n, 0);
    assert_null(evs);

    /* 空查询 = 全量 */
    memset(&q, 0, sizeof(q));
    evs = NULL;
    n = run_query(&q, &evs);
    assert_int_equal(n, 2);
    assert_non_null(evs);
    free_query(evs, n);
    printf("  [info] query none-match: 0 rows with NULL events\n");
}

/* 导出：全量导出与日志文件字节一致；条件导出条数正确 */
static void test_audit_export(void **state) {
    audit_ctx_t *t = (audit_ctx_t *)*state;
    if (!g_ops) {
        skip();
        return;
    }
    append_ev(1000, "alice", "login", "console", 0, "auth ok");
    append_ev(2000, "alice", "write", "/etc/app.conf", -EACCES, "permission denied");
    append_ev(3000, "bob", "login", "console", 0, "auth ok");

    /* 全量导出 */
    hw_audit_query_t q;
    memset(&q, 0, sizeof(q));
    uint64_t cnt = 0;
    assert_int_equal(g_ops->export_events(&q, t->exp, &cnt), 0);
    assert_int_equal(cnt, 3);

    char *logbuf = NULL, *expbuf = NULL;
    size_t loglen = 0, explen = 0;
    assert_int_equal(read_file(t->log, &logbuf, &loglen), 0);
    assert_int_equal(read_file(t->exp, &expbuf, &explen), 0);
    assert_int_equal(loglen, explen);
    assert_memory_equal(logbuf, expbuf, loglen);
    free(logbuf);
    free(expbuf);

    /* 条件导出：仅 login（2 条） */
    char exp2[380];
    snprintf(exp2, sizeof(exp2), "%s/export2.log", t->dir);
    memset(&q, 0, sizeof(q));
    snprintf(q.action, sizeof(q.action), "%s", "login");
    cnt = 0;
    assert_int_equal(g_ops->export_events(&q, exp2, &cnt), 0);
    assert_int_equal(cnt, 2);

    FILE *f = fopen(exp2, "r");
    assert_non_null(f);
    char line[512];
    int lines = 0;
    int only_login = 1;
    while (fgets(line, sizeof(line), f)) {
        lines++;
        if (!strstr(line, "|login|")) only_login = 0;
    }
    fclose(f);
    assert_int_equal(lines, 2);
    assert_int_equal(only_login, 1);

    printf("  [info] export: full == audit.log (%zu bytes), filtered=2 login rows\n", loglen);
}

/* 容量滚动：max_bytes 触发、保留 max_files 段、丢弃最旧、最新仍在、seq 连续 */
static void test_audit_rotate(void **state) {
    audit_ctx_t *t = (audit_ctx_t *)*state;
    if (!g_ops) {
        skip();
        return;
    }
    /* 小容量：单文件约 240B（约 8~9 行），仅保留 3 段 → 大量旧事件被滚动丢弃 */
    assert_int_equal(g_p->ops.configure(g_p, HW_AUDIT_PARAM_MAX_BYTES, "240"), 0);
    assert_int_equal(g_p->ops.configure(g_p, HW_AUDIT_PARAM_MAX_FILES, "3"), 0);

    const int total = 60;
    for (int i = 0; i < total; i++)
        append_ev(2000000000, "svc", "tick", "", 0, NULL);

    /* 主文件存在、非空且受单文件上限约束（允许单行超出的小量冗余） */
    assert_int_equal(access(t->log, F_OK), 0);
    struct stat st;
    assert_int_equal(stat(t->log, &st), 0);
    assert_true(st.st_size > 0);
    assert_true(st.st_size <= 400);

    /* 主文件 + 至少 1 个滚动段存在（3 段制实际滚动多次后应 3 段齐全） */
    int segs = 0;
    for (int i = 0; i < 32; i++) {
        char p[380];
        if (i == 0)
            snprintf(p, sizeof(p), "%s/audit.log", t->dir);
        else
            snprintf(p, sizeof(p), "%s/audit.log.%d", t->dir, i);
        if (access(p, F_OK) == 0) segs++;
    }
    assert_true(segs >= 2);

    /* 全量查询 = 当前保留行数：旧事件已被丢弃（远小于 60），新事件保留 */
    hw_audit_event_t *evs = NULL;
    hw_audit_query_t q;
    memset(&q, 0, sizeof(q));
    int kept = run_query(&q, &evs);
    assert_true(kept >= 1);
    assert_true(kept < total);
    free_query(evs, kept);

    /* 最新一条事件仍可查（seq == total） */
    memset(&q, 0, sizeof(q));
    q.order = HW_AUDIT_ORDER_DESC;
    q.limit = 1;
    evs = NULL;
    int n = run_query(&q, &evs);
    assert_int_equal(n, 1);
    assert_int_equal(evs[0].seq, (uint64_t)total);
    assert_string_equal(evs[0].subject, "svc");
    assert_string_equal(evs[0].action, "tick");
    free_query(evs, n);

    /* seq 全局连续（status：已分配 60，下一 61），滚动不影响计数 */
    hw_audit_status_t stt;
    memset(&stt, 0, sizeof(stt));
    assert_int_equal(g_ops->status(&stt), 0);
    assert_int_equal(stt.total_events, (uint64_t)total);
    assert_int_equal(stt.seq_next, (uint64_t)total + 1);
    assert_string_equal(stt.dir, t->dir);

    printf("  [info] rotate: appended=%d, retained=%d in %d segments, newest seq=%d "
           "still queryable\n",
           total, kept, segs, total);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_audit_append_and_file, audit_test_setup,
                                        audit_test_teardown),
        cmocka_unit_test_setup_teardown(test_audit_query_filters, audit_test_setup,
                                        audit_test_teardown),
        cmocka_unit_test_setup_teardown(test_audit_query_none, audit_test_setup,
                                        audit_test_teardown),
        cmocka_unit_test_setup_teardown(test_audit_export, audit_test_setup, audit_test_teardown),
        cmocka_unit_test_setup_teardown(test_audit_rotate, audit_test_setup, audit_test_teardown),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
