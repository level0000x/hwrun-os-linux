/*
 * libtest_lib.c — 供 LOADER 插件 load/dlopen 自测用的最小共享库
 */
int loader_test_lib_value(void) {
    return 42;
}