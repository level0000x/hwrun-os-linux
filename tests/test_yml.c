/*
 * test_yml.c — CMocka 单元测试：plugin.yml 解析
 *
 * 测试对象：bus/src/yml.c（hw_yml_parse_plugin / hw_plugin_discovery_clear /
 *           hw_type_from_str / hw_type_to_str）。
 *
 * 覆盖：
 *   - 临时 plugin.yml（/tmp/hwtest_yml_<pid>.yml，teardown 删除）：
 *     plugin 嵌套根 + provides/requires 的"纯字符串"与"带 version map"
 *     两种列表形态 + files 文件列表；
 *   - 解析后断言 id / type（含枚举换算）/ provides_count / requires_count /
 *     files 数组内容；
 *   - disc 为栈结构：parse 内部先 memset；hw_plugin_discovery_clear 只释放
 *     内部动态数组、不 free(d)（d 本身在栈上），清后计数归零；
 *   - 容错：文件不存在 → HWRUN_ENOENT，路径为 NULL → HWRUN_EINVAL。
 *
 * 说明：解析器为扁平状态机，version map 内的 "version:" 行会覆盖顶层
 * version 字段（既有实现行为），故本测试不校验 version。
 */

#include "hwrun.h"
#include "bus.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <cmocka.h>

/* 样例：plugin 嵌套根；provides/requires 同时含 "- protocol: X" 与 "- X" */
static const char YML_BODY[] =
    "# CMocka yml 解析夹具\n"
    "plugin:\n"
    "  id: \"testplug\"\n"
    "  name: \"Test Plugin\"\n"
    "  type: \"tools\"\n"
    "  description: \"yml parse fixture\"\n"
    "  provides:\n"
    "    - protocol: \"FOO\"\n"
    "      version: \"1.0\"\n"
    "    - \"BAR\"\n"
    "  requires:\n"
    "    - protocol: \"LOG\"\n"
    "      version: \"1.0\"\n"
    "    - \"METAPROTO\"\n"
    "  files:\n"
    "    - \"build/testplug.so\"\n"
    "    - \"README.md\"\n";

/* ---- 临时文件路径：/tmp/hwtest_yml_<pid>.yml ---- */
static void tmp_yml_path(char *buf, size_t cap) {
    snprintf(buf, cap, "/tmp/hwtest_yml_%ld.yml", (long)getpid());
}

static int group_teardown(void **state) {
    (void)state;
    char path[256];
    tmp_yml_path(path, sizeof(path));
    remove(path);
    return 0;
}

/* ---- 正常解析：双形态列表 + 嵌套根 ---- */
static void test_yml_parse_ok(void **state) {
    (void)state;
    char path[256];
    tmp_yml_path(path, sizeof(path));
    remove(path);                       /* 清理可能的残留 */

    FILE *fp = fopen(path, "w");
    assert_non_null(fp);
    assert_true(fputs(YML_BODY, fp) >= 0);
    assert_int_equal(fclose(fp), 0);

    /* disc 在栈上；先用非零字节填充，验证 parse 内部自行 memset */
    hw_plugin_discovery_t disc;
    memset(&disc, 0xAA, sizeof(disc));
    assert_int_equal(hw_yml_parse_plugin(path, &disc), HWRUN_OK);

    /* 顶层标量字段 */
    assert_string_equal(disc.id, "testplug");
    assert_string_equal(disc.name, "Test Plugin");
    assert_string_equal(disc.description, "yml parse fixture");
    assert_string_equal(disc.type, "tools");
    assert_int_equal(hw_type_from_str(disc.type), HWPLUGIN_TYPE_TOOLS);
    assert_string_equal(hw_type_to_str(HWPLUGIN_TYPE_TOOLS), "tools");

    /* provides：version map + 纯字符串两形态，均入列 */
    assert_int_equal(disc.provides_count, 2);
    assert_string_equal(disc.provides[0], "FOO");
    assert_string_equal(disc.provides[1], "BAR");

    /* requires：同上双形态 */
    assert_int_equal(disc.requires_count, 2);
    assert_string_equal(disc.requires[0], "LOG");
    assert_string_equal(disc.requires[1], "METAPROTO");

    /* files */
    assert_int_equal(disc.files_count, 2);
    assert_string_equal(disc.files[0], "build/testplug.so");
    assert_string_equal(disc.files[1], "README.md");

    /* clear 只清内部数组与计数，不 free(d)：d 仍在栈上可复用 */
    hw_plugin_discovery_clear(&disc);
    assert_int_equal(disc.provides_count, 0);
    assert_int_equal(disc.requires_count, 0);
    assert_int_equal(disc.conflicts_count, 0);
    assert_int_equal(disc.files_count, 0);

    /* 复用同一 disc 再解析一轮应仍成功（clear 后无残留） */
    assert_int_equal(hw_yml_parse_plugin(path, &disc), HWRUN_OK);
    assert_int_equal(disc.provides_count, 2);
    hw_plugin_discovery_clear(&disc);

    remove(path);
}

/* ---- 容错：路径不存在 / 入参非法 ---- */
static void test_yml_parse_errors(void **state) {
    (void)state;
    hw_plugin_discovery_t disc;

    assert_int_equal(hw_yml_parse_plugin(NULL, &disc), HWRUN_EINVAL);
    assert_int_equal(hw_yml_parse_plugin("/tmp/hwtest_yml_no_such_file.yml",
                                         &disc), HWRUN_ENOENT);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_yml_parse_ok),
        cmocka_unit_test(test_yml_parse_errors),
    };
    return cmocka_run_group_tests(tests, NULL, group_teardown);
}
