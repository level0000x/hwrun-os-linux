/*
 * test_storage.c — CMocka 单元测试：STORAGE 存储后端协议（用户态卷管理）
 *
 * 测试对象：storage/build/storage.so 插件及其 provides 协议 "STORAGE" 的
 * hw_storage_ops_t。真实语义：卷即目录，create 时确保目录存在；统计由
 * du+statvfs 真实取值；快照为目录级逐字节副本；注册表持久化到
 * $HWRUN_STATE/storage.state（dlopen 前注入临时区，绝不触碰 /var/lib/hwrun）。
 *
 * 链路（与 test_compress.c 一致）：
 *   dlopen(.so) -> hw_plugin_entry() -> init()
 *   -> metaproto_register(provides, implementation=get_interface("STORAGE"))
 *   -> start -> metaproto_resolve("STORAGE") -> hw_storage_ops_t*
 *
 * 覆盖要点（全部真文件系统操作）：
 *   - create_volume：目录被真实创建、list/get 可取回、重名/重路径拒绝、
 *     非法名称/路径拒绝、只读卷 delete 被拒；
 *   - volume_df：du 真实占用/文件数 >= 写入内容，statvfs 字段与直接调用一致；
 *   - snapshot：快照目录与源内容逐字节一致、恢复后源回到快照版本、
 *     list_snapshots 按源卷过滤；
 *   - delete/unregister：delete 删目录+元数据，unregister 仅摘元数据留目录；
 *   - 持久化重载：同 storage.state 下 stop/destroy/dlclose 再 init 重载后
 *     卷与快照仍在、数据目录与内容完好。
 *
 * 清理：teardown 卸插件后递归删除 /tmp/hwtest_storage_* 临时区并还原 HWRUN_STATE。
 */

#include "hwrun.h"
#include "bus.h"
#include "metaproto.h"
#include "../storage/include/storage.h"

#include <dirent.h>
#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cmocka.h>

#ifndef HWRUN_PLUGIN_ROOT
#define HWRUN_PLUGIN_ROOT "../"
#endif

#define STORAGE_TEST_PREFIX "/tmp/hwtest_storage_"

static hw_bus_t g_bus;
static void *g_h = NULL; /* dlopen 句柄 */
static hw_plugin_t *g_p = NULL;
static hw_storage_ops_t *g_ops = NULL;
static char g_base[256]; /* 临时状态目录（HWRUN_STATE）；插件要求 <240 字 */

/* ============================================================
 * 文件/目录工具（测试侧）
 * ============================================================ */
static void rm_tmp_tree(const char *path) {
    DIR *d = opendir(path);
    if (!d) {
        remove(path);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char child[600];
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        struct stat st;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode))
            rm_tmp_tree(child);
        else
            remove(child);
    }
    closedir(d);
    rmdir(path);
}

static void write_bytes(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    assert_non_null(f);
    assert_int_equal(fwrite(data, 1, len, f), len);
    fclose(f);
}

static void write_text(const char *path, const char *text) {
    write_bytes(path, text, strlen(text));
}

/* 逐字节比较两个文件，全等返回 0 */
static int files_equal(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb");
    FILE *fb = fopen(b, "rb");
    if (!fa || !fb) {
        if (fa) fclose(fa);
        if (fb) fclose(fb);
        return -1;
    }
    int eq = 1;
    char ba[8192], bb[8192];
    for (;;) {
        size_t ra = fread(ba, 1, sizeof(ba), fa);
        size_t rb = fread(bb, 1, sizeof(bb), fb);
        if (ra != rb || memcmp(ba, bb, ra) != 0) {
            eq = 0;
            break;
        }
        if (ra < sizeof(ba)) break; /* 双方同时 EOF */
    }
    fclose(fa);
    fclose(fb);
    return eq ? 0 : -1;
}

static void fill_pattern(uint8_t *buf, size_t len, unsigned seed) {
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)((i * 131U + seed) & 0xffU);
}

/* 创建卷的测试助手：卷目录落在临时区 base/vol_<name> */
static void do_create_volume(const char *name, int readonly, uint64_t limit,
                             hw_storage_volume_info_t *info, char *path_out, size_t path_cap) {
    char path[512];
    snprintf(path, sizeof(path), "%s/vol_%s", g_base, name);
    if (path_out) snprintf(path_out, path_cap, "%s", path);
    hw_storage_volume_def_t def;
    memset(&def, 0, sizeof(def));
    def.name = name;
    def.path = path;
    def.size_limit = limit;
    def.readonly = readonly;
    def.description = "storage test volume";
    assert_int_equal(g_ops->create_volume(&def, info), 0);
    assert_true(info->id[0] != '\0');
    assert_true(info->exists);
}

/* ============================================================
 * 组级 setup：总线 + dlopen + 生命周期 + 注册 provides
 * ============================================================ */
static int plugin_load_once(void) {
    char so[512];
    snprintf(so, sizeof(so), HWRUN_PLUGIN_ROOT "storage/build/storage.so");
    g_h = dlopen(so, RTLD_NOW | RTLD_GLOBAL);
    if (!g_h) {
        printf("  [skip] dlopen(%s): %s\n", so, dlerror());
        return -1;
    }
    hw_plugin_t *(*entry)(void) = (hw_plugin_t * (*)(void)) dlsym(g_h, "hw_plugin_entry");
    if (!entry) {
        printf("  [skip] %s: 无 hw_plugin_entry\n", so);
        dlclose(g_h);
        g_h = NULL;
        return -1;
    }
    hw_plugin_t *self = entry();
    if (!self) {
        dlclose(g_h);
        g_h = NULL;
        return -1;
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
    if (hw_metaproto_resolve(&g_bus.meta, "STORAGE", NULL, &r) == HWRUN_OK && r &&
        r->implementation)
        g_ops = (hw_storage_ops_t *)r->implementation;
    else
        printf("  [warn] resolve(STORAGE) 未取得实现指针\n");
    return 0;
}

static void plugin_unload_once(void) {
    if (!g_p) return;
    if (g_p->ops.stop) g_p->ops.stop(g_p);
    for (int i = 0; i < g_p->provides_count; i++)
        hw_metaproto_unregister(&g_bus.meta, g_p->provides[i], g_p->id);
    if (g_p->ops.destroy) g_p->ops.destroy(g_p);
    dlclose(g_h);
    g_h = NULL;
    g_p = NULL;
    g_ops = NULL;
}

static int group_setup(void **state) {
    (void)state;
    if (hw_bus_init(&g_bus, NULL, NULL, HWLOG_WARN) != HWRUN_OK) {
        fprintf(stderr, "test_storage: hw_bus_init 失败\n");
        return -1;
    }
    /* 1) /tmp/hwtest_storage_<pid> 临时状态区；dlopen 前注入 HWRUN_STATE */
    snprintf(g_base, sizeof(g_base), STORAGE_TEST_PREFIX "%ld", (long)getpid());
    rm_tmp_tree(g_base);
    if (mkdir(g_base, 0755) != 0) {
        fprintf(stderr, "test_storage: mkdir(%s): %s\n", g_base, strerror(errno));
        return -1;
    }
    setenv("HWRUN_STATE", g_base, 1);

    /* 2) dlopen + 生命周期 + 注册 + resolve */
    if (plugin_load_once() != 0) return 0;
    return 0;
}

static int group_teardown(void **state) {
    (void)state;
    plugin_unload_once();
    hw_bus_shutdown(&g_bus);
    rm_tmp_tree(g_base);
    g_base[0] = '\0';
    unsetenv("HWRUN_STATE");
    return 0;
}

/* ============================================================
 * 用例
 * ============================================================ */

/* 卷注册表：create/list/get、重名/重路径/非法输入拒绝、只读卷禁删 */
static void test_storage_volume_crud(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    char pa[512], pb[512];
    hw_storage_volume_info_t ia, ib;
    memset(&ia, 0, sizeof(ia));
    memset(&ib, 0, sizeof(ib));

    /* create：目录被真实创建 + 元数据登记 */
    do_create_volume("alpha", 1, 2ULL * 1024 * 1024, &ia, pa, sizeof(pa));
    assert_int_equal(access(pa, F_OK), 0);
    hw_storage_volume_info_t got;
    memset(&got, 0, sizeof(got));
    assert_int_equal(g_ops->get_volume("alpha", &got), 0);
    assert_string_equal(got.id, ia.id);
    assert_string_equal(got.path, pa);
    assert_true(got.readonly);
    assert_true(got.size_limit == 2ULL * 1024 * 1024);
    assert_true(got.created_at > 0);
    assert_true(got.exists);

    /* 重名卷 / 同路径不同名 → -EEXIST */
    hw_storage_volume_def_t dup;
    memset(&dup, 0, sizeof(dup));
    dup.name = "alpha";
    dup.path = pa;
    assert_int_equal(g_ops->create_volume(&dup, &ib), -EEXIST);
    dup.name = "alpha2";
    dup.path = pa;
    assert_int_equal(g_ops->create_volume(&dup, &ib), -EEXIST);

    /* 非法名称/路径 → -EINVAL */
    dup.name = "bad/name";
    dup.path = pa;
    assert_int_equal(g_ops->create_volume(&dup, &ib), -EINVAL);
    dup.name = "badpath";
    dup.path = "relative/path";
    assert_int_equal(g_ops->create_volume(&dup, &ib), -EINVAL);

    /* list：能看到 alpha/beta 且 id 不空 */
    do_create_volume("beta", 0, 0, &ib, pb, sizeof(pb));
    hw_storage_volume_info_t list[16];
    int count = 0;
    assert_int_equal(g_ops->list_volumes(list, 16, &count), 0);
    assert_true(count >= 2);
    int hit_a = 0, hit_b = 0;
    for (int i = 0; i < count; i++) {
        if (!strcmp(list[i].name, "alpha")) hit_a = 1;
        if (!strcmp(list[i].name, "beta")) hit_b = 1;
        assert_true(list[i].id[0] != '\0');
    }
    assert_true(hit_a && hit_b);

    /* 只读卷禁删数据，注销（仅元数据）仍可保留目录 */
    assert_int_equal(g_ops->delete_volume("alpha"), -EPERM);
    assert_int_equal(g_ops->unregister_volume("alpha"), 0);
    assert_int_equal(access(pa, F_OK), 0); /* 数据目录原样保留 */
    assert_int_equal(g_ops->get_volume("alpha", &got), -ENOENT);

    /* 正常卷 delete：删目录 + 摘元数据；二次删除 -ENOENT */
    assert_int_equal(g_ops->delete_volume("beta"), 0);
    assert_int_equal(access(pb, F_OK), -1);
    assert_int_equal(g_ops->get_volume("beta", &got), -ENOENT);
    assert_int_equal(g_ops->delete_volume("beta"), -ENOENT);
    assert_int_equal(g_ops->delete_volume("no-such-vol"), -ENOENT);

    rm_tmp_tree(pa); /* 收尾：清理 unregister 后保留的 alpha 数据目录 */
}

/* 统计：du 真实占用 + statvfs 与直接系统调用一致 */
static void test_storage_df_real(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    char pv[512];
    hw_storage_volume_info_t info;
    memset(&info, 0, sizeof(info));
    do_create_volume("stats", 0, 0, &info, pv, sizeof(pv));

    /* 卷内写入：100KB + 嵌套 5KB + 空文件 */
    uint8_t *big = malloc(100 * 1024);
    assert_non_null(big);
    fill_pattern(big, 100 * 1024, 7);
    char f1[560], subdir[560], f2[560], f3[560];
    snprintf(f1, sizeof(f1), "%s/data.bin", pv);
    snprintf(subdir, sizeof(subdir), "%s/sub", pv);
    snprintf(f2, sizeof(f2), "%s/sub/nested.bin", pv);
    snprintf(f3, sizeof(f3), "%s/empty.txt", pv);
    write_bytes(f1, big, 100 * 1024);
    assert_int_equal(mkdir(subdir, 0755), 0);
    write_bytes(f2, big + 500, 5 * 1024);
    write_text(f3, "");
    free(big);

    hw_storage_df_t df;
    memset(&df, 0, sizeof(df));
    assert_int_equal(g_ops->volume_df("stats", &df), 0);
    assert_true(df.used_bytes >= 105 * 1024); /* 真实 du >= 写入字节和 */
    assert_true(df.file_count >= 3);

    /* statvfs 字段与直接系统调用一致（真实值校验） */
    struct statvfs sv;
    assert_int_equal(statvfs(pv, &sv), 0);
    unsigned long frs = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
    assert_true(df.fs_block_size == sv.f_bsize);
    assert_true(df.fs_total == (uint64_t)sv.f_blocks * (uint64_t)frs);
    assert_true(df.fs_avail == (uint64_t)sv.f_bavail * (uint64_t)frs);
    assert_true(df.fs_used == (uint64_t)(sv.f_blocks - sv.f_bfree) * (uint64_t)frs);
    assert_true(df.fs_total > 0 && df.fs_avail > 0);
    assert_true(df.fs_used <= df.fs_total);
    printf("  [info] stats: used=%llu files=%llu fs_total=%llu fs_avail=%llu\n",
           (unsigned long long)df.used_bytes, (unsigned long long)df.file_count,
           (unsigned long long)df.fs_total, (unsigned long long)df.fs_avail);

    assert_int_equal(g_ops->delete_volume("stats"), 0);
}

/* 快照：真实副本内容一致、恢复回滚、按卷过滤 */
static void test_storage_snapshot(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    char pv[512];
    hw_storage_volume_info_t info;
    memset(&info, 0, sizeof(info));
    do_create_volume("snapv", 0, 0, &info, pv, sizeof(pv));

    /* 源内容：d1.txt + sub/note.txt */
    char d1[560], sub[560], note[560];
    snprintf(d1, sizeof(d1), "%s/d1.txt", pv);
    snprintf(sub, sizeof(sub), "%s/sub", pv);
    snprintf(note, sizeof(note), "%s/sub/note.txt", pv);
    write_text(d1, "hello-storage-v1");
    assert_int_equal(mkdir(sub, 0755), 0);
    write_text(note, "nested-content");

    hw_storage_snapshot_info_t si;
    memset(&si, 0, sizeof(si));
    assert_int_equal(g_ops->snapshot_create("snapv", "s1", &si), 0);
    assert_true(si.id[0] != '\0');
    assert_int_equal(access(si.path, F_OK), 0);

    /* 快照副本内容与源一致（逐字节） */
    char sd1[560], snote[560];
    snprintf(sd1, sizeof(sd1), "%s/d1.txt", si.path);
    snprintf(snote, sizeof(snote), "%s/sub/note.txt", si.path);
    assert_int_equal(files_equal(d1, sd1), 0);
    assert_int_equal(files_equal(note, snote), 0);

    /* 同名快照重试 → -EEXIST */
    assert_int_equal(g_ops->snapshot_create("snapv", "s1", &si), -EEXIST);

    /* 修改源后恢复：内容回到快照版本 */
    write_text(d1, "hello-storage-v2-changed");
    assert_int_equal(files_equal(d1, sd1), -1); /* 与快照不再一致 */
    assert_int_equal(g_ops->snapshot_restore("s1", NULL), 0);
    assert_int_equal(files_equal(d1, sd1), 0); /* 恢复后 == 快照版本 */
    assert_int_equal(files_equal(note, snote), 0);

    /* list 与按源卷过滤：另建 snapw/w1 后，过滤 snapv 不得含 w1 */
    char pw[512];
    hw_storage_volume_info_t iw;
    memset(&iw, 0, sizeof(iw));
    do_create_volume("snapw", 0, 0, &iw, pw, sizeof(pw));
    hw_storage_snapshot_info_t wi;
    memset(&wi, 0, sizeof(wi));
    assert_int_equal(g_ops->snapshot_create("snapw", "w1", &wi), 0);

    hw_storage_snapshot_info_t slist[16];
    int count = 0;
    assert_int_equal(g_ops->list_snapshots(NULL, slist, 16, &count), 0);
    assert_true(count >= 2);
    assert_int_equal(g_ops->list_snapshots("snapv", slist, 16, &count), 0);
    assert_true(count >= 1);
    int has_w = 0;
    for (int i = 0; i < count; i++) {
        assert_string_equal(slist[i].volume, "snapv");
        if (!strcmp(slist[i].name, "w1")) has_w = 1;
    }
    assert_false(has_w);

    /* 删除快照：目录消失、登记摘除 */
    assert_int_equal(g_ops->snapshot_delete("s1"), 0);
    assert_int_equal(access(si.path, F_OK), -1);
    assert_int_equal(g_ops->snapshot_delete("s1"), -ENOENT);
    assert_int_equal(g_ops->snapshot_delete("w1"), 0);

    assert_int_equal(g_ops->delete_volume("snapv"), 0);
    assert_int_equal(g_ops->delete_volume("snapw"), 0);
}

/* 注销语义：仅摘元数据，数据目录保留 */
static void test_storage_unregister_keeps_data(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    char pk[512];
    hw_storage_volume_info_t info;
    memset(&info, 0, sizeof(info));
    do_create_volume("keep", 0, 0, &info, pk, sizeof(pk));

    char f[560];
    snprintf(f, sizeof(f), "%s/payload.txt", pk);
    write_text(f, "keep-me");

    assert_int_equal(g_ops->unregister_volume("keep"), 0);
    assert_int_equal(access(pk, F_OK), 0); /* 目录仍在 */
    assert_int_equal(access(f, F_OK), 0);  /* 数据仍在 */
    hw_storage_volume_info_t got;
    memset(&got, 0, sizeof(got));
    assert_int_equal(g_ops->get_volume("keep", &got), -ENOENT);
    int count = 0;
    hw_storage_volume_info_t list[16];
    memset(list, 0, sizeof(list));
    assert_int_equal(g_ops->list_volumes(list, 16, &count), 0);
    for (int i = 0; i < count; i++)
        assert_true(strcmp(list[i].name, "keep") != 0);

    rm_tmp_tree(pk); /* 收尾清理保留的数据目录 */
}

/* 持久化重载：同 storage.state 重载后卷/快照仍在、数据完好 */
static void test_storage_persist_reload(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    char pp[512];
    hw_storage_volume_info_t info;
    memset(&info, 0, sizeof(info));
    do_create_volume("persistvol", 0, 0, &info, pp, sizeof(pp));

    char f[560];
    snprintf(f, sizeof(f), "%s/marker.txt", pp);
    write_text(f, "persist-marker-content");

    hw_storage_snapshot_info_t si;
    memset(&si, 0, sizeof(si));
    assert_int_equal(g_ops->snapshot_create("persistvol", "ps1", &si), 0);

    /* 卸载并重载：模拟重启恢复 */
    plugin_unload_once();
    assert_int_equal(plugin_load_once(), 0);
    assert_non_null(g_ops);

    hw_storage_volume_info_t got;
    memset(&got, 0, sizeof(got));
    assert_int_equal(g_ops->get_volume("persistvol", &got), 0); /* 重启后卷仍在 */
    assert_string_equal(got.name, "persistvol");
    assert_string_equal(got.path, pp);
    assert_true(got.exists);
    assert_int_equal(access(f, F_OK), 0); /* 数据目录与内容完好 */

    int count = 0;
    hw_storage_snapshot_info_t slist[8];
    memset(slist, 0, sizeof(slist));
    assert_int_equal(g_ops->list_snapshots("persistvol", slist, 8, &count), 0);
    assert_int_equal(count, 1);
    assert_string_equal(slist[0].name, "ps1");
    assert_int_equal(access(slist[0].path, F_OK), 0);

    /* 重载后的注册表可正常变更（删快照 + 删卷） */
    assert_int_equal(g_ops->snapshot_delete("ps1"), 0);
    assert_int_equal(g_ops->delete_volume("persistvol"), 0);
    assert_int_equal(access(pp, F_OK), -1);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_storage_volume_crud),
        cmocka_unit_test(test_storage_df_real),
        cmocka_unit_test(test_storage_snapshot),
        cmocka_unit_test(test_storage_unregister_keeps_data),
        cmocka_unit_test(test_storage_persist_reload),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
