/*
 * test_param.c — CMocka 单元测试：PARAM 参数树全套
 *
 * 测试对象：bus/src/param.c + bus/src/hwrun.c 参数树底层（hw_param_*）。
 *
 * 覆盖：
 *   - init / shutdown（shutdown 后 ctx->root == NULL、get 返回 NULL）；
 *   - set_value + get / get_int / get_bool（含缺省值回退与 bool 形态识别）；
 *   - watch：设值后 watcher 收到 old/new；首次设置 old 为 NULL；
 *     pattern 用 fnmatch 通配过滤（"sched.*" 不命中 "net.port"）；
 *   - save_file / load_file 往返：临时文件 /tmp/hwtest_param_*.conf，
 *     测毕由 group teardown 删除，不残留仓库内文件。
 *
 * 用例全部单线程，不经 hw_locker_enable_parallel()（锁为 no-op 亦可跑通）。
 */

#include "hwrun.h"
#include "param.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <cmocka.h>

/* ---- watcher 回调记录 ---- */
typedef struct watch_rec {
    int  calls;            /* 收到通知次数      */
    char last_key[128];
    char last_old[256];    /* old==NULL 时记为空串 */
    char last_new[256];
    int  old_null_count;   /* old 为 NULL 的次数 */
} watch_rec_t;

static int watch_cb(const char *key, const char *old_v, const char *new_v,
                    void *userdata) {
    watch_rec_t *r = (watch_rec_t *)userdata;
    r->calls++;
    snprintf(r->last_key, sizeof(r->last_key), "%s", key ? key : "");
    if (old_v) {
        snprintf(r->last_old, sizeof(r->last_old), "%s", old_v);
    } else {
        r->last_old[0] = '\0';
        r->old_null_count++;
    }
    snprintf(r->last_new, sizeof(r->last_new), "%s", new_v ? new_v : "");
    return 0;
}

/* ---- 临时文件路径：/tmp/hwtest_param_<pid>.conf ---- */
static void tmp_conf_path(char *buf, size_t cap) {
    snprintf(buf, cap, "/tmp/hwtest_param_%ld.conf", (long)getpid());
}

static int group_teardown(void **state) {
    (void)state;
    char path[256];
    tmp_conf_path(path, sizeof(path));
    remove(path);
    return 0;
}

/* ---- 基础读写与类型转换 ---- */
static void test_param_basic_rw(void **state) {
    (void)state;
    hw_param_context_t ctx;

    assert_int_equal(hw_param_init(&ctx, NULL), HWRUN_OK);

    /* 缺省值：未设置的 key 回退默认 */
    assert_null(hw_param_get(&ctx, "no.such.key"));
    assert_int_equal(hw_param_get_int(&ctx, "no.such.key", -7), -7);
    assert_true(hw_param_get_bool(&ctx, "no.such.key", true));
    assert_false(hw_param_get_bool(&ctx, "no.such.key", false));

    /* string */
    assert_int_equal(hw_param_set_value(&ctx, "sched.policy", "rr",
                                        HWPARAM_TYPE_STRING, "调度策略"),
                     HWRUN_OK);
    const char *v = hw_param_get(&ctx, "sched.policy");
    assert_non_null(v);
    assert_string_equal(v, "rr");

    /* 覆盖写 */
    assert_int_equal(hw_param_set_value(&ctx, "sched.policy", "fifo",
                                        HWPARAM_TYPE_STRING, NULL), HWRUN_OK);
    assert_string_equal(hw_param_get(&ctx, "sched.policy"), "fifo");

    /* int */
    assert_int_equal(hw_param_set_value(&ctx, "net.port", "8080",
                                        HWPARAM_TYPE_INT, NULL), HWRUN_OK);
    assert_int_equal(hw_param_get_int(&ctx, "net.port", -1), 8080);
    assert_int_equal(hw_param_get_int(&ctx, "net.port", 12345), 8080);

    /* bool 形态识别 */
    assert_int_equal(hw_param_set_value(&ctx, "feat.enable", "true",
                                        HWPARAM_TYPE_BOOL, NULL), HWRUN_OK);
    assert_true(hw_param_get_bool(&ctx, "feat.enable", false));
    assert_int_equal(hw_param_set_value(&ctx, "feat.enable", "yes",
                                        HWPARAM_TYPE_BOOL, NULL), HWRUN_OK);
    assert_true(hw_param_get_bool(&ctx, "feat.enable", false));
    assert_int_equal(hw_param_set_value(&ctx, "feat.enable", "no",
                                        HWPARAM_TYPE_BOOL, NULL), HWRUN_OK);
    assert_false(hw_param_get_bool(&ctx, "feat.enable", true));
    assert_int_equal(hw_param_set_value(&ctx, "feat.num", "0",
                                        HWPARAM_TYPE_BOOL, NULL), HWRUN_OK);
    assert_false(hw_param_get_bool(&ctx, "feat.num", true));
    /* 无法识别的布尔值 → 回退默认 */
    assert_int_equal(hw_param_set_value(&ctx, "feat.weird", "maybe",
                                        HWPARAM_TYPE_BOOL, NULL), HWRUN_OK);
    assert_true(hw_param_get_bool(&ctx, "feat.weird", true));

    /* shutdown 后置 NULL，一切 get 返回缺省 */
    hw_param_shutdown(&ctx);
    assert_null(ctx.root);
    assert_int_equal(ctx.initialized, 0);
    assert_null(hw_param_get(&ctx, "sched.policy"));
    assert_int_equal(hw_param_get_int(&ctx, "net.port", 99), 99);
}

/* ---- watch 通知：触发、old/new、fnmatch pattern 过滤 ---- */
static void test_param_watch(void **state) {
    (void)state;
    hw_param_context_t ctx;
    watch_rec_t all;
    watch_rec_t sched;

    assert_int_equal(hw_param_init(&ctx, NULL), HWRUN_OK);
    memset(&all, 0, sizeof(all));
    memset(&sched, 0, sizeof(sched));

    /* pattern ""=监听全部；"sched.*" 仅命中 sched 前缀 */
    assert_int_equal(hw_param_watch(&ctx, "watcher-all", "", watch_cb, &all),
                     HWRUN_OK);
    assert_int_equal(hw_param_watch(&ctx, "watcher-sched", "sched.*",
                                    watch_cb, &sched), HWRUN_OK);
    /* 非法入参 → EINVAL */
    assert_int_equal(hw_param_watch(&ctx, NULL, NULL, watch_cb, NULL),
                     HWRUN_EINVAL);
    assert_int_equal(hw_param_watch(&ctx, "watcher-x", NULL, NULL, NULL),
                     HWRUN_EINVAL);

    /* 首次设置：old == NULL */
    assert_int_equal(hw_param_set_value(&ctx, "sched.policy", "fifo",
                                        HWPARAM_TYPE_STRING, NULL), HWRUN_OK);
    assert_int_equal(all.calls, 1);
    assert_string_equal(all.last_key, "sched.policy");
    assert_int_equal(all.old_null_count, 1);
    assert_string_equal(all.last_old, "");
    assert_string_equal(all.last_new, "fifo");
    assert_int_equal(sched.calls, 1);

    /* 再次设置：watcher 收到 old 与 new */
    assert_int_equal(hw_param_set_value(&ctx, "sched.policy", "cfs",
                                        HWPARAM_TYPE_STRING, NULL), HWRUN_OK);
    assert_int_equal(all.calls, 2);
    assert_string_equal(all.last_old, "fifo");
    assert_string_equal(all.last_new, "cfs");
    assert_int_equal(sched.calls, 2);

    /* 不匹配 "sched.*" 的 key：全量 watcher 命中，pattern watcher 不命中 */
    assert_int_equal(hw_param_set_value(&ctx, "net.port", "9999",
                                        HWPARAM_TYPE_INT, NULL), HWRUN_OK);
    assert_int_equal(all.calls, 3);
    assert_string_equal(all.last_key, "net.port");
    assert_int_equal(sched.calls, 2);

    hw_param_shutdown(&ctx);
}

/* ---- save_file / load_file 往返 ---- */
static void test_param_file_roundtrip(void **state) {
    (void)state;
    char path[256];
    tmp_conf_path(path, sizeof(path));
    remove(path);                       /* 清理可能的残留 */

    hw_param_context_t ctx;
    assert_int_equal(hw_param_init(&ctx, NULL), HWRUN_OK);
    assert_int_equal(hw_param_set_value(&ctx, "net.ip", "10.0.0.7",
                                        HWPARAM_TYPE_STRING, NULL), HWRUN_OK);
    assert_int_equal(hw_param_set_value(&ctx, "net.port", "8080",
                                        HWPARAM_TYPE_INT, NULL), HWRUN_OK);
    assert_int_equal(hw_param_set_value(&ctx, "sched.policy", "fifo",
                                        HWPARAM_TYPE_STRING, NULL), HWRUN_OK);
    assert_int_equal(hw_param_set_value(&ctx, "feat.enable", "true",
                                        HWPARAM_TYPE_BOOL, NULL), HWRUN_OK);
    assert_int_equal(hw_param_save_file(&ctx, path), HWRUN_OK);
    hw_param_shutdown(&ctx);

    /* 磁盘内容抽查：扁平 "key: value" */
    {
        FILE *fp = fopen(path, "r");
        assert_non_null(fp);
        char content[1024] = "";
        size_t n = fread(content, 1, sizeof(content) - 1, fp);
        content[n] = '\0';
        fclose(fp);
        assert_non_null(strstr(content, "sched.policy: fifo"));
        assert_non_null(strstr(content, "net.ip: 10.0.0.7"));
    }

    /* 新上下文 load 回来：值与类型读取一致 */
    hw_param_context_t ctx2;
    assert_int_equal(hw_param_init(&ctx2, NULL), HWRUN_OK);
    assert_int_equal(hw_param_load_file(&ctx2, path, HWPARAM_USER), HWRUN_OK);
    assert_string_equal(hw_param_get(&ctx2, "net.ip"), "10.0.0.7");
    assert_int_equal(hw_param_get_int(&ctx2, "net.port", -1), 8080);
    assert_string_equal(hw_param_get(&ctx2, "sched.policy"), "fifo");
    assert_true(hw_param_get_bool(&ctx2, "feat.enable", false));

    /* load 不存在的文件 → ENOENT */
    assert_int_equal(hw_param_load_file(&ctx2, "/tmp/hwtest_param_missing.conf",
                                        HWPARAM_USER), HWRUN_ENOENT);

    hw_param_shutdown(&ctx2);
    remove(path);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_param_basic_rw),
        cmocka_unit_test(test_param_watch),
        cmocka_unit_test(test_param_file_roundtrip),
    };
    return cmocka_run_group_tests(tests, NULL, group_teardown);
}
