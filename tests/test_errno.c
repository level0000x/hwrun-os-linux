/*
 * test_errno.c — CMocka 单元测试：错误码宏语义 + 基础工具函数
 *
 * 测试对象：
 *   - include/hwrun.h 的错误码宏（HWRUN_OK / HWRUN_E* / HW_EBASE 私有域）；
 *   - bus/src/hwrun.c 的 hw_str_eq / hw_fmt_path。
 *
 * 要点：
 *   - HWRUN_OK == 0，HWRUN_E* 一律为负 errno 别名；
 *   - HWRUN_ECONFLICT / HWRUN_ENOTREADY 落在 HW_EBASE 私有域
 *     （负值且绝对值远大于标准 errno 上限 ~133）；
 *   - hw_str_eq 的 NULL 语义（NULL==NULL 视为相等）；
 *   - hw_fmt_path 目录尾斜杠有无两种拼接正确，超长/非法入参返回负错误码。
 */

#include "hwrun.h"

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

/* ---- 错误码宏语义 ---- */
static void test_error_code_semantics(void **state) {
    (void)state;

    /* 成功唯一值 0 */
    assert_int_equal(HWRUN_OK, 0);

    /* 标准 errno 一律负化别名 */
    assert_int_equal(HWRUN_EPERM, -EPERM);
    assert_int_equal(HWRUN_ENOENT, -ENOENT);
    assert_int_equal(HWRUN_ENOMEM, -ENOMEM);
    assert_int_equal(HWRUN_EEXIST, -EEXIST);
    assert_int_equal(HWRUN_ENOTSUP, -ENOTSUP);
    assert_int_equal(HWRUN_EILSEQ, -EILSEQ);
    assert_int_equal(HWRUN_EAGAIN, -EAGAIN);
    assert_int_equal(HWRUN_EINPROGRESS, -EINPROGRESS);
    assert_int_equal(HWRUN_EINVAL, -EINVAL);

    /* 私有域：负值、低于 -(HW_EBASE)、绝对值 > 标准 errno 上限 133 */
    assert_true(HWRUN_ECONFLICT < 0);
    assert_true(HWRUN_ECONFLICT < -(HW_EBASE));
    assert_true(-HWRUN_ECONFLICT > 133);
    assert_true(HWRUN_ENOTREADY < -(HW_EBASE));
    assert_true(-HWRUN_ENOTREADY > 133);

    /* 私有错误互相独立，且不与标准 errno 别名冲突 */
    assert_int_not_equal(HWRUN_ECONFLICT, HWRUN_ENOTREADY);
    assert_int_not_equal(HWRUN_ECONFLICT, -ENOENT);
    assert_int_not_equal(HWRUN_ECONFLICT, -EINVAL);
    assert_int_not_equal(HWRUN_ECONFLICT, -ENOTSUP);
}

/* ---- hw_str_eq：NULL 语义与普通相等 ---- */
static void test_hw_str_eq(void **state) {
    (void)state;

    assert_true(hw_str_eq(NULL, NULL));
    assert_false(hw_str_eq(NULL, "x"));
    assert_false(hw_str_eq("x", NULL));
    assert_true(hw_str_eq("abc", "abc"));
    assert_false(hw_str_eq("abc", "abd"));
    assert_false(hw_str_eq("abc", "abcd"));
}

/* ---- hw_fmt_path：路径拼接与越界/入参保护 ---- */
static void test_hw_fmt_path(void **state) {
    (void)state;

    char buf[256];
    /* 目录无尾斜杠 */
    assert_int_equal(hw_fmt_path(buf, sizeof(buf), "/etc/hwrun", "git.so"),
                     HWRUN_OK);
    assert_string_equal(buf, "/etc/hwrun/git.so");
    /* 目录带尾斜杠 */
    assert_int_equal(hw_fmt_path(buf, sizeof(buf), "/etc/hwrun/", "git.so"),
                     HWRUN_OK);
    assert_string_equal(buf, "/etc/hwrun/git.so");
    /* 根目录 "/" */
    assert_int_equal(hw_fmt_path(buf, sizeof(buf), "/", "a.so"), HWRUN_OK);
    assert_string_equal(buf, "/a.so");

    /* 容量不足：截断并返回负错误码（HWRUN_ENOMEM） */
    char small[8];
    int rc = hw_fmt_path(small, sizeof(small), "/very/long/directory/path",
                         "name.bin");
    assert_true(rc < 0);
    assert_int_equal(rc, HWRUN_ENOMEM);

    /* 非法入参 → HWRUN_EINVAL */
    assert_int_equal(hw_fmt_path(NULL, sizeof(buf), "d", "n"), HWRUN_EINVAL);
    assert_int_equal(hw_fmt_path(buf, 0, "d", "n"), HWRUN_EINVAL);
    assert_int_equal(hw_fmt_path(buf, sizeof(buf), NULL, "n"), HWRUN_EINVAL);
    assert_int_equal(hw_fmt_path(buf, sizeof(buf), "d", NULL), HWRUN_EINVAL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_error_code_semantics),
        cmocka_unit_test(test_hw_str_eq),
        cmocka_unit_test(test_hw_fmt_path),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
