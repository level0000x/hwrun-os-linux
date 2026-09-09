/*
 * test_permission.c — CMocka 单元测试：PERMISSION 权限协议（dlopen .so 后真实调用）
 *
 * 测试对象：permission/build/permission.so 插件及其 provides 协议 "PERMISSION" 的
 * hw_permission_ops_t（真实用户态 RBAC，非打桩）。插件零第三方依赖，自包含，
 * 单独 dlopen 即可真跑。
 *
 * 链路（与 test_compress.c 一致）：
 *   dlopen(.so) -> hw_plugin_entry() -> init()
 *   -> metaproto_register(provides, implementation=get_interface("PERMISSION"))
 *   -> start -> metaproto_resolve("PERMISSION") -> hw_permission_ops_t*
 *
 * 覆盖要点（全部真 RBAC 裁决）：
 *   A 组（空状态起步）：默认拒绝、角色 CRUD、grant/revoke、check 允许/拒绝、
 *     动作/资源通配匹配、role_list/binding_list、写操作自动落盘；
 *   B 组（模拟"重启"：A 组 teardown dlclose 后重新 dlopen 新实例，
 *     start 自动从状态文件载入）：验证策略重启可恢复 + 拒绝语义仍生效。
 *
 * 状态目录：mkdtemp 于 /tmp 并 setenv HWRUN_STATE，插件默认状态路径 =
 * $HWRUN_STATE/permission.state；测试结束清理。
 * 错误约定：成功返回 0，失败返回负 errno；check 拒绝返回 -EACCES。
 */

#define _GNU_SOURCE
#include "hwrun.h"
#include "bus.h"
#include "metaproto.h"
#include "../permission/include/permission.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <setjmp.h>
#include <dlfcn.h>
#include <cmocka.h>

#ifndef HWRUN_PLUGIN_ROOT
#define HWRUN_PLUGIN_ROOT "../"
#endif

static hw_bus_t g_bus;
static void *g_h = NULL; /* dlopen 句柄 */
static hw_plugin_t *g_p = NULL;
static hw_permission_ops_t *g_ops = NULL;

static char g_state_dir[128];  /* mkdtemp /tmp 状态目录（跨组保留） */
static char g_state_file[256]; /* <dir>/permission.state（插件默认路径） */
static char g_snap_file[256];  /* 显式 save/load 用快照文件 */

/* ---- 组级 setup：总线 + dlopen + 生命周期 + 注册 provides + resolve ---- */
static int group_setup(void **state) {
    (void)state;
    if (hw_bus_init(&g_bus, NULL, NULL, HWLOG_WARN) != HWRUN_OK) {
        fprintf(stderr, "test_permission: hw_bus_init 失败\n");
        return -1;
    }

    char so[512];
    snprintf(so, sizeof(so), HWRUN_PLUGIN_ROOT "permission/build/permission.so");
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
    if (self->ops.start) self->ops.start(self); /* B 组这里自动载入既有状态文件 */
    self->state = HWPLUGIN_STARTED;
    g_p = self;

    hw_protocol_route_t *r = NULL;
    if (hw_metaproto_resolve(&g_bus.meta, "PERMISSION", NULL, &r) == HWRUN_OK && r &&
        r->implementation)
        g_ops = (hw_permission_ops_t *)r->implementation;
    else
        printf("  [warn] resolve(PERMISSION) 未取得实现指针\n");
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

/* ---- 辅助 ---- */
static int role_exists(const char *name) {
    hw_permission_role_t roles[16];
    memset(roles, 0, sizeof(roles));
    int count = 0;
    if (g_ops->role_list(roles, 16, &count) != 0) return 0;
    for (int i = 0; i < count && i < 16; i++)
        if (strcmp(roles[i].name, name) == 0) return 1;
    return 0;
}

static int binding_exists(const char *subject, const char *role) {
    hw_permission_binding_t bs[32];
    memset(bs, 0, sizeof(bs));
    int count = 0;
    if (g_ops->binding_list(bs, 32, &count) != 0) return 0;
    for (int i = 0; i < count && i < 32; i++)
        if (strcmp(bs[i].subject, subject) == 0 && strcmp(bs[i].role, role) == 0) return 1;
    return 0;
}

static int file_contains(const char *path, const char *needle) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, needle) != NULL;
}

/* ============================================================
 * A 组：空状态起步，功能全覆盖（写操作自动落盘到状态目录）
 * ============================================================ */

/* 空状态默认拒绝 */
static void test_default_deny(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    assert_int_equal(g_ops->check("ghost", "anything", "res"), -EACCES);
    assert_int_equal(g_ops->check("root", "fs.read", "/etc"), -EACCES);
    assert_int_equal(g_ops->check(NULL, "fs.read", "/etc"), -EINVAL);
    assert_int_equal(g_ops->check("root", NULL, "/etc"), -EINVAL);
    assert_int_equal(g_ops->role_get("nope", &(hw_permission_role_t){0}), -ENOENT);
    printf("  [info] 默认拒绝：未知主体/空策略均 -EACCES\n");
}

/* 角色 CRUD + 权限条目 */
static void test_role_crud(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    assert_int_equal(g_ops->role_add("admin", "系统管理员"), 0);
    assert_int_equal(g_ops->role_add("admin", "dup"), -EEXIST);
    /* 非法名字/描述 */
    assert_int_equal(g_ops->role_add("bad name", ""), -EINVAL);
    assert_int_equal(g_ops->role_add("bad#name", ""), -EINVAL);
    assert_int_equal(g_ops->role_add("", "x"), -EINVAL);

    hw_permission_role_t role;
    memset(&role, 0, sizeof(role));
    assert_int_equal(g_ops->role_get("admin", &role), 0);
    assert_string_equal(role.name, "admin");
    assert_string_equal(role.description, "系统管理员");
    assert_int_equal(role.perm_count, 0);

    assert_int_equal(g_ops->role_add_perm("admin", "*.*", NULL), 0);
    assert_int_equal(g_ops->role_add_perm("admin", "fs.write", "/home/*"), 0);
    assert_int_equal(g_ops->role_add_perm("admin", "*.*", NULL), -EEXIST); /* 重复条目 */
    assert_int_equal(g_ops->role_add_perm("ghost", "a.b", NULL), -ENOENT);

    memset(&role, 0, sizeof(role));
    assert_int_equal(g_ops->role_get("admin", &role), 0);
    assert_int_equal(role.perm_count, 2);
    assert_string_equal(role.perms[0].action, "*.*");
    assert_int_equal(role.perms[0].resource[0], '\0');
    assert_string_equal(role.perms[1].action, "fs.write");
    assert_string_equal(role.perms[1].resource, "/home/*");

    assert_int_equal(g_ops->role_remove_perm("admin", "fs.write", "/home/*"), 0);
    assert_int_equal(g_ops->role_remove_perm("admin", "fs.write", "/home/*"), -ENOENT);
    memset(&role, 0, sizeof(role));
    assert_int_equal(g_ops->role_get("admin", &role), 0);
    assert_int_equal(role.perm_count, 1);

    assert_int_equal(g_ops->role_list(NULL, 8, &(int){0}), -EINVAL);
    assert_true(role_exists("admin"));
    assert_int_equal(g_ops->role_remove("admin"), 0);
    assert_int_equal(g_ops->role_remove("admin"), -ENOENT);
    assert_int_equal(g_ops->role_get("admin", &role), -ENOENT);
    printf("  [info] 角色 CRUD：增/查/改权限/删 全通过\n");
}

/* grant/revoke + check 允许/拒绝（默认拒绝） */
static void test_grant_check(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    assert_int_equal(g_ops->role_add("reader", "只读用户"), 0);
    assert_int_equal(g_ops->role_add_perm("reader", "fs.read", NULL), 0);

    assert_int_equal(g_ops->grant("alice", "reader"), 0);
    assert_int_equal(g_ops->grant("alice", "reader"), -EEXIST);
    assert_int_equal(g_ops->grant("alice", "ghost"), -ENOENT);
    assert_int_equal(g_ops->grant("bad subject", "reader"), -EINVAL);

    assert_int_equal(g_ops->check("alice", "fs.read", "/etc/hwrun"), 0);
    assert_int_equal(g_ops->check("alice", "fs.read", ""), 0); /* 资源模式空=任意 */
    assert_int_equal(g_ops->check("alice", "fs.write", "/etc/hwrun"), -EACCES);

    assert_int_equal(g_ops->grant("bob", "reader"), 0);
    assert_int_equal(g_ops->check("bob", "fs.read", "/var/lib/hwrun"), 0);
    assert_true(binding_exists("alice", "reader"));
    assert_true(binding_exists("bob", "reader"));

    /* 撤销后回到拒绝 */
    assert_int_equal(g_ops->revoke("alice", "reader"), 0);
    assert_int_equal(g_ops->check("alice", "fs.read", "/etc"), -EACCES);
    assert_int_equal(g_ops->revoke("alice", "reader"), -ENOENT);
    assert_int_equal(g_ops->revoke("bob", "reader"), 0);
    assert_int_equal(g_ops->check("bob", "fs.read", "/var/lib"), -EACCES);
    printf("  [info] grant/revoke：授予即允许、撤销即拒绝\n");
}

/* 动作/资源通配匹配 */
static void test_wildcard_match(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    assert_int_equal(g_ops->role_add("sre", "运维工程师"), 0);
    assert_int_equal(g_ops->role_add_perm("sre", "cluster.*", NULL), 0);
    assert_int_equal(g_ops->role_add_perm("sre", "task.*", "/jobs/*"), 0);
    assert_int_equal(g_ops->grant("sre-1", "sre"), 0);

    /* 动作通配 cluster.* */
    assert_int_equal(g_ops->check("sre-1", "cluster.node.join", "cluster-1"), 0);
    assert_int_equal(g_ops->check("sre-1", "cluster.leave", ""), 0);
    /* 动作+资源双重约束：task.* 且资源在 /jobs/ 目录子树下 */
    assert_int_equal(g_ops->check("sre-1", "task.submit", "/jobs/42"), 0);
    assert_int_equal(g_ops->check("sre-1", "task.submit", "/etc/passwd"), -EACCES);
    /* 有资源约束的条目对"空资源"不适用 */
    assert_int_equal(g_ops->check("sre-1", "task.submit", ""), -EACCES);
    /* 动作不匹配 */
    assert_int_equal(g_ops->check("sre-1", "fs.read", "/jobs/42"), -EACCES);
    printf("  [info] 通配：cluster.* / /jobs/* 匹配正确\n");
}

/* role_list / binding_list */
static void test_listing(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    hw_permission_role_t roles[16];
    memset(roles, 0, sizeof(roles));
    int count = 0;
    assert_int_equal(g_ops->role_list(roles, 16, &count), 0);
    assert_true(count >= 2);
    assert_true(role_exists("reader"));
    assert_true(role_exists("sre"));

    hw_permission_binding_t bs[32];
    memset(bs, 0, sizeof(bs));
    int bcount = 0;
    assert_int_equal(g_ops->binding_list(bs, 32, &bcount), 0);
    assert_true(bcount >= 1);
    assert_true(binding_exists("sre-1", "sre"));
    printf("  [info] list：role_list/binding_list 返回真实项\n");
}

/* 持久化：写操作自动落盘到 $HWRUN_STATE/permission.state */
static void test_persist_disk(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    assert_int_equal(g_ops->role_add("ops", "运维角色"), 0);
    assert_int_equal(g_ops->role_add_perm("ops", "task.submit", NULL), 0);
    assert_int_equal(g_ops->grant("svc-0", "ops"), 0);
    assert_int_equal(g_ops->check("svc-0", "task.submit", "node-1"), 0);
    /* 状态文件已随写操作自动落盘 */
    assert_int_equal(access(g_state_file, F_OK), 0);
    assert_true(file_contains(g_state_file, "ops"));
    assert_true(file_contains(g_state_file, "svc-0"));
    printf("  [info] 持久化：状态已自动写入 %s\n", g_state_file);
}

/* 显式 save/load（快照回滚 + 写回） */
static void test_explicit_save_load(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    assert_int_equal(g_ops->save(g_snap_file), 0); /* 快照：当前含 ops/svc-0/reader/sre */
    assert_int_equal(access(g_snap_file, F_OK), 0);

    assert_int_equal(g_ops->role_add("late", "临时角色"), 0);
    assert_int_equal(g_ops->role_get("late", &(hw_permission_role_t){0}), 0);
    /* 从快照重新载入：整体替换内存，late 消失 */
    assert_int_equal(g_ops->load(g_snap_file), 0);
    assert_int_equal(g_ops->role_get("late", &(hw_permission_role_t){0}), -ENOENT);
    /* 载入不存在的文件 -> -ENOENT */
    assert_int_equal(g_ops->load("/tmp/hwperm_no_such_file_xyz.state"), -ENOENT);
    /* 内存写回默认路径，使磁盘与内存一致 */
    assert_int_equal(g_ops->save(NULL), 0);
    printf("  [info] 显式 save/load：快照回滚与写回一致\n");
}

/* ============================================================
 * B 组：模拟重启 —— 重新 dlopen 新实例，start 自动载入状态文件
 * ============================================================ */
static void test_restart_restore(void **state) {
    (void)state;
    if (!g_ops) {
        skip();
        return;
    }
    /* A 组末态策略应全部恢复：ops(+svc-0)、sre(+sre-1)、reader */
    assert_true(role_exists("ops"));
    assert_true(role_exists("sre"));
    assert_true(role_exists("reader"));
    assert_true(binding_exists("svc-0", "ops"));
    assert_true(binding_exists("sre-1", "sre"));

    hw_permission_role_t role;
    memset(&role, 0, sizeof(role));
    assert_int_equal(g_ops->role_get("ops", &role), 0);
    assert_string_equal(role.description, "运维角色");

    /* 允许路径恢复 */
    assert_int_equal(g_ops->check("svc-0", "task.submit", "node-1"), 0);
    assert_int_equal(g_ops->check("sre-1", "task.submit", "/jobs/9"), 0);
    assert_int_equal(g_ops->check("sre-1", "cluster.node.join", "c1"), 0);
    /* 拒绝语义在重启后仍然生效（默认拒绝） */
    assert_int_equal(g_ops->check("sre-1", "task.submit", "/etc"), -EACCES);
    assert_int_equal(g_ops->check("svc-0", "cluster.node.join", "c1"), -EACCES);
    assert_int_equal(g_ops->check("mallory", "fs.read", "/x"), -EACCES);

    /* 重启后可继续变更并再次落盘 */
    assert_int_equal(g_ops->revoke("svc-0", "ops"), 0);
    assert_int_equal(g_ops->check("svc-0", "task.submit", "node-1"), -EACCES);
    assert_false(binding_exists("svc-0", "ops"));
    printf("  [info] 重启恢复：start 自动载入状态文件，策略与拒绝语义均生效\n");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    /* 准备 /tmp 状态目录；插件默认状态路径 = $HWRUN_STATE/permission.state */
    snprintf(g_state_dir, sizeof(g_state_dir), "/tmp/hwperm_XXXXXX");
    if (!mkdtemp(g_state_dir)) {
        fprintf(stderr, "test_permission: mkdtemp 失败\n");
        return 2;
    }
    snprintf(g_state_file, sizeof(g_state_file), "%s/permission.state", g_state_dir);
    snprintf(g_snap_file, sizeof(g_snap_file), "%s/snapshot.state", g_state_dir);
    setenv("HWRUN_STATE", g_state_dir, 1);

    const struct CMUnitTest group_a[] = {
        cmocka_unit_test(test_default_deny),
        cmocka_unit_test(test_role_crud),
        cmocka_unit_test(test_grant_check),
        cmocka_unit_test(test_wildcard_match),
        cmocka_unit_test(test_listing),
        cmocka_unit_test(test_persist_disk),
        cmocka_unit_test(test_explicit_save_load),
    };
    const struct CMUnitTest group_b[] = {
        cmocka_unit_test(test_restart_restore),
    };

    /* A 组结束 dlclose 卸插件 -> B 组重新 dlopen = 一次"进程重启" */
    int rc1 = cmocka_run_group_tests(group_a, group_setup, group_teardown);
    int rc2 = cmocka_run_group_tests(group_b, group_setup, group_teardown);

    /* 清理 /tmp 状态目录 */
    unsetenv("HWRUN_STATE");
    remove(g_state_file);
    remove(g_snap_file);
    rmdir(g_state_dir);
    return rc1 ? rc1 : rc2;
}
