/*
 * test_lock.c — CMocka 单元测试：hwlock 并发正确性
 *
 * 测试对象：bus/src/hwlock.c + bus/src/metaproto.c + bus/src/param.c 在
 *          hw_locker_enable_parallel() 之后的真实锁并发行为。
 *
 * 覆盖：
 *   - 4 线程对同一 hw_metaproto_registry_t 同时 hammer register（新增+更新）
 *     / resolve / unregister 数千次：无崩溃，无数据竞争导致的计数偏差；
 *     结束后注册表仅剩永久路由（计数一致）、所有永久协议仍可解析；
 *   - 4 线程对同一 hw_param_context_t 并发 set_value/get：无崩溃，所有写
 *     返回 OK，每线程独占 key 终值一致，共享 key 终值格式合法（最后写入者胜）。
 *
 * 说明：
 *   - 主线程在 run_tests 前调用 hw_locker_enable_parallel() 武装全局锁；
 *   - worker 线程只累加结果、不调用 cmocka 断言（断言统一在主线程 join 后做）；
 *   - 各线程操作各自命名的协议/参数 key，交叉操作经由共享链表与锁完成，
 *     互不释放他人节点，保证最终断言确定无 flaky。
 */

#include "hwrun.h"
#include "hwlock.h"
#include "metaproto.h"
#include "param.h"

#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <cmocka.h>

/* ============================================================
 * 并发 1：METAPROTO 注册表 hammer
 * ============================================================ */
#define MP_THREADS 4
#define MP_ITERS 2000 /* 每线程迭代：2 register + 2 resolve + 1 unregister */
#define MP_PERM 8     /* 永久协议数（只读解析目标） */

typedef struct mp_arg {
    hw_metaproto_registry_t *reg;
    int tid;
    long registers_ok; /* 每迭代 2 次：新增分支 + 更新分支 */
    long resolves_ok;  /* 命中自己刚注册且已更新的协议 */
    long perm_hits;    /* 命中永久协议 */
    long unregisters_ok;
} mp_arg_t;

static mp_arg_t g_mp_args[MP_THREADS];

static void *mp_worker(void *arg) {
    mp_arg_t *a = (mp_arg_t *)arg;
    char proto[64];

    for (int i = 0; i < MP_ITERS; i++) {
        /* 新增分支 */
        snprintf(proto, sizeof(proto), "LOCKP%d_%d", a->tid, i);
        if (hw_metaproto_register(a->reg, proto, "1.0", "thr", (void *)(intptr_t)(0x1000 + i)) ==
            HWRUN_OK)
            a->registers_ok++;
        /* 更新分支：同插件同协议重注册（热替换实现） */
        if (hw_metaproto_register(a->reg, proto, "1.0", "thr", (void *)(intptr_t)0xBEEF) ==
            HWRUN_OK)
            a->registers_ok++;

        /* 解析自己的协议：必然命中且实现已被更新为新指针 */
        hw_protocol_route_t *r = NULL;
        if (hw_metaproto_resolve(a->reg, proto, NULL, &r) == HWRUN_OK && r &&
            r->implementation == (void *)(intptr_t)0xBEEF)
            a->resolves_ok++;

        /* 解析永久协议：始终命中 */
        snprintf(proto, sizeof(proto), "PERM%d", i % MP_PERM);
        r = NULL;
        if (hw_metaproto_resolve(a->reg, proto, NULL, &r) == HWRUN_OK && r) a->perm_hits++;

        /* 注销自己的协议 */
        snprintf(proto, sizeof(proto), "LOCKP%d_%d", a->tid, i);
        if (hw_metaproto_unregister(a->reg, proto, "thr") == HWRUN_OK) a->unregisters_ok++;
    }
    return NULL;
}

static void test_metaproto_concurrent(void **state) {
    (void)state;
    hw_metaproto_registry_t reg;
    assert_int_equal(hw_metaproto_init(&reg, NULL), HWRUN_OK);

    /* 先注册永久协议：hammer 期间只读解析、绝不注销 */
    for (int i = 0; i < MP_PERM; i++) {
        char p[32];
        snprintf(p, sizeof(p), "PERM%d", i);
        assert_int_equal(
            hw_metaproto_register(&reg, p, "1.0", "perm", (void *)(intptr_t)(0x2000 + i)),
            HWRUN_OK);
    }

    pthread_t th[MP_THREADS];
    int created = 0;
    for (int t = 0; t < MP_THREADS; t++) {
        memset(&g_mp_args[t], 0, sizeof(g_mp_args[t]));
        g_mp_args[t].reg = &reg;
        g_mp_args[t].tid = t;
        if (pthread_create(&th[t], NULL, mp_worker, &g_mp_args[t]) != 0) break;
        created++;
    }
    for (int t = 0; t < created; t++)
        assert_int_equal(pthread_join(th[t], NULL), 0);
    assert_int_equal(created, MP_THREADS);

    /* 逐线程计数精确：所有 register/resolve/unregister 均成功 */
    long tot_reg = 0, tot_res = 0, tot_perm = 0, tot_unreg = 0;
    for (int t = 0; t < created; t++) {
        assert_int_equal(g_mp_args[t].registers_ok, (long)MP_ITERS * 2);
        assert_int_equal(g_mp_args[t].resolves_ok, (long)MP_ITERS);
        assert_int_equal(g_mp_args[t].perm_hits, (long)MP_ITERS);
        assert_int_equal(g_mp_args[t].unregisters_ok, (long)MP_ITERS);
        tot_reg += g_mp_args[t].registers_ok;
        tot_res += g_mp_args[t].resolves_ok;
        tot_perm += g_mp_args[t].perm_hits;
        tot_unreg += g_mp_args[t].unregisters_ok;
    }
    assert_int_equal(tot_reg, (long)MP_THREADS * MP_ITERS * 2);
    assert_int_equal(tot_res, (long)MP_THREADS * MP_ITERS);
    assert_int_equal(tot_perm, (long)MP_THREADS * MP_ITERS);
    assert_int_equal(tot_unreg, (long)MP_THREADS * MP_ITERS);

    /* 结束时注册表计数一致：仅剩 8 条永久路由，且内容未被破坏 */
    hw_protocol_route_t **arr = NULL;
    int cnt = 0;
    assert_int_equal(hw_metaproto_list(&reg, &arr, &cnt), HWRUN_OK);
    assert_int_equal(cnt, MP_PERM);
    free(arr);
    for (int i = 0; i < MP_PERM; i++) {
        char p[32];
        hw_protocol_route_t *r = NULL;
        snprintf(p, sizeof(p), "PERM%d", i);
        assert_int_equal(hw_metaproto_resolve(&reg, p, NULL, &r), HWRUN_OK);
        assert_non_null(r);
        assert_ptr_equal(r->implementation, (void *)(intptr_t)(0x2000 + i));
    }

    hw_metaproto_shutdown(&reg);
}

/* ============================================================
 * 并发 2：PARAM 参数树 hammer
 * ============================================================ */
#define PM_THREADS 4
#define PM_ITERS 3000

typedef struct pm_arg {
    hw_param_context_t *ctx;
    int tid;
    long sets_ok; /* 共享 key 写成功次数 */
} pm_arg_t;

static pm_arg_t g_pm_args[PM_THREADS];

static void *pm_worker(void *arg) {
    pm_arg_t *a = (pm_arg_t *)arg;
    char key[64];
    char val[64];

    for (int i = 0; i < PM_ITERS; i++) {
        /* 共享 key：全部线程并发写，值含线程号与序号 */
        snprintf(val, sizeof(val), "t%d:%d", a->tid, i);
        if (hw_param_set_value(a->ctx, "conc.counter", val, HWPARAM_TYPE_STRING, NULL) == HWRUN_OK)
            a->sets_ok++;
        /* 每线程独占 key：固定 int 哨兵 */
        snprintf(key, sizeof(key), "conc.t%d", a->tid);
        snprintf(val, sizeof(val), "%d", 1000 + a->tid);
        (void)hw_param_set_value(a->ctx, key, val, HWPARAM_TYPE_INT, NULL);
        /* 读压力：并发 get 不崩即可（返回值不在此处断言） */
        (void)hw_param_get_int(a->ctx, "conc.counter", -1);
    }
    return NULL;
}

static void test_param_concurrent(void **state) {
    (void)state;
    hw_param_context_t ctx;
    assert_int_equal(hw_param_init(&ctx, NULL), HWRUN_OK);

    pthread_t th[PM_THREADS];
    int created = 0;
    for (int t = 0; t < PM_THREADS; t++) {
        memset(&g_pm_args[t], 0, sizeof(g_pm_args[t]));
        g_pm_args[t].ctx = &ctx;
        g_pm_args[t].tid = t;
        if (pthread_create(&th[t], NULL, pm_worker, &g_pm_args[t]) != 0) break;
        created++;
    }
    for (int t = 0; t < created; t++)
        assert_int_equal(pthread_join(th[t], NULL), 0);
    assert_int_equal(created, PM_THREADS);

    /* 所有写成功；每线程独占 key 终值与哨兵一致 */
    long tot_sets = 0;
    for (int t = 0; t < created; t++) {
        char key[64];
        snprintf(key, sizeof(key), "conc.t%d", t);
        assert_int_equal(g_pm_args[t].sets_ok, (long)PM_ITERS);
        assert_int_equal(hw_param_get_int(&ctx, key, -1), 1000 + t);
        tot_sets += g_pm_args[t].sets_ok;
    }
    assert_int_equal(tot_sets, (long)PM_THREADS * PM_ITERS);

    /* 共享 key 终值格式合法：t<tid>:<i>，最后写入者胜 */
    char final[256];
    const char *v = hw_param_get(&ctx, "conc.counter");
    assert_non_null(v);
    snprintf(final, sizeof(final), "%s", v);
    int tid = -1;
    long idx = -1;
    assert_int_equal(sscanf(final, "t%d:%ld", &tid, &idx), 2);
    assert_true(tid >= 0 && tid < PM_THREADS);
    assert_true(idx >= 0 && idx < (long)PM_ITERS);

    hw_param_shutdown(&ctx);
}

int main(void) {
    hw_locker_enable_parallel(); /* 武装全局锁：全部 locker 进入真实并发模式 */
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_metaproto_concurrent),
        cmocka_unit_test(test_param_concurrent),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
