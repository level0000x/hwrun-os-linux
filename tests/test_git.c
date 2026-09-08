/*
 * test_git.c — CMocka 单元测试：GIT 版本控制协议（dlopen .so 后真实调用）
 *
 * 测试对象：git/build/git.so 插件及其 provides 协议 "GIT" 的 hw_git_ops_t。
 * GIT 是"薄封装"：所有实际操作委托系统 /usr/bin/git 命令（executor 经
 * popen 在 git_g_ctx.repo_path 下执行），故本测试需要在真实 git 工作区跑。
 *
 * 关键机制：git 插件所有 ops 都作用于全局 git_g_ctx.repo_path/audit_path，
 * init(path) 只对给定目录做一次 git init，不会切换后续 ops 的工作目录。
 * 因此组级 setup 在 dlopen 前把环境变量 HWRUN_STATE 指向 /tmp/hwtest_git_<pid>，
 * 并在其下写 git.conf（repo_path/audit_path 均指入 /tmp），使插件 init 时
 * git_config_load 就把仓库/审计目录落到测试临时区，绝不触碰 /var/lib/hwrun。
 *
 * 链路（与 test_proto.c 的 load_plugin 模式一致）：
 *   dlopen(.so) -> hw_plugin_entry() -> init()（含 ensure_repo：git init 临时库）
 *   -> metaproto_register(provides) -> start
 *   -> metaproto_resolve("GIT") -> hw_git_ops_t*
 *
 * 覆盖要点（Linux + git 已装时可全量验证）：
 *   - get_config：HWRUN_STATE/git.conf 生效，repo_path == 临时仓库、branch=main；
 *   - 真实工作流：写文件 -> status(含未跟踪文件) -> add -> commit(返回 hash)
 *     -> log(1) 非空且 hash/message 正确 -> free_commits -> status(干净非空)；
 *   - 提交身份：用 system("git -C <repo> config user.name/email ...") 只配置
 *     临时仓库（不触碰全局配置）；
 *   - size：仓库打包（compact(0) 触发真实 repack/gc）后 > 0。
 *
 * 环境缺失（无可用 git 命令 / .so 未构建）：打印 [skip] 后宽松通过，
 * 保证 Windows/MSYS2 等无 git 环境不误报；Linux 目标环境真实跑通。
 *
 * 清理：group teardown 先 stop/unregister/destroy/dlclose 插件，
 * 再递归删除 /tmp/hwtest_git_* 临时目录（仅限该前缀）并还原 HWRUN_STATE。
 * p 是 .so 静态描述符，不得 free(p)；不挂入 bus->plugins。
 */

#include "hwrun.h"
#include "bus.h"
#include "metaproto.h"
#include "../git/include/git.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <setjmp.h>
#include <dlfcn.h>
#include <cmocka.h>

#ifndef HWRUN_PLUGIN_ROOT
#define HWRUN_PLUGIN_ROOT "../"
#endif

/* 临时目录前缀（防误删：rm 前必须校验此前缀） */
#define GIT_TMP_PREFIX "/tmp/hwtest_git_"
#define GIT_TMP_PREFIX_LEN (sizeof(GIT_TMP_PREFIX) - 1)

static hw_bus_t g_bus;
static void *g_h = NULL;        /* dlopen 句柄            */
static hw_plugin_t *g_p = NULL; /* .so 静态描述符，不 free */
static hw_git_ops_t *g_ops = NULL;

static char g_base[256];  /* /tmp/hwtest_git_<pid> */
static char g_repo[256];  /* <base>/repo            */
static char g_audit[256]; /* <base>/audit           */

/* ---- 临时目录工具 ---- */

/* 仅删除 /tmp/hwtest_git_* 前缀目录（安全护栏），其余路径直接忽略 */
static void rm_tmp_tree(const char *path) {
    if (!path || strncmp(path, GIT_TMP_PREFIX, GIT_TMP_PREFIX_LEN) != 0) return;
    char cmd[640];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", path);
    (void)system(cmd);
}

static int write_git_conf(void) {
    char conf[512];
    snprintf(conf, sizeof(conf), "%s/git.conf", g_base);
    FILE *fp = fopen(conf, "w");
    if (!fp) {
        fprintf(stderr, "test_git: 无法写入 %s: %s\n", conf, strerror(errno));
        return -1;
    }
    fprintf(fp, "repo_path=%s\n", g_repo);
    fprintf(fp, "audit_path=%s\n", g_audit);
    fprintf(fp, "branch=main\n");
    fprintf(fp, "auto_commit=1\n");
    fprintf(fp, "auto_push=0\n");
    fprintf(fp, "auto_compact=1\n");
    fprintf(fp, "max_size=1073741824\n");
    fclose(fp);
    return 0;
}

/* ---- 组级 setup ---- */
static int group_setup(void **state) {
    (void)state;
    if (hw_bus_init(&g_bus, NULL, NULL, HWLOG_WARN) != HWRUN_OK) {
        fprintf(stderr, "test_git: hw_bus_init 失败\n");
        return -1;
    }

    /* 1) 准备 /tmp/hwtest_git_<pid> 临时区与 git.conf */
    snprintf(g_base, sizeof(g_base), GIT_TMP_PREFIX "%ld", (long)getpid());
    snprintf(g_repo, sizeof(g_repo), "%s/repo", g_base);
    snprintf(g_audit, sizeof(g_audit), "%s/audit", g_base);

    rm_tmp_tree(g_base); /* 清理上次崩溃残留 */
    if (mkdir(g_base, 0755) != 0) {
        fprintf(stderr, "test_git: mkdir(%s): %s\n", g_base, strerror(errno));
        return -1;
    }
    if (write_git_conf() != 0) {
        rm_tmp_tree(g_base);
        return -1;
    }
    /* dlopen 前注入：插件 init 的 git_config_load 读取 $HWRUN_STATE/git.conf */
    setenv("HWRUN_STATE", g_base, 1);

    /* 2) dlopen + 生命周期 + 注册 */
    char so[512];
    snprintf(so, sizeof(so), HWRUN_PLUGIN_ROOT "git/build/git.so");
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

    if (self->ops.init) self->ops.init(self); /* 内含 ensure_repo：git init 临时库 */
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
    if (hw_metaproto_resolve(&g_bus.meta, HWPROTO_GIT, NULL, &r) == HWRUN_OK && r &&
        r->implementation)
        g_ops = (hw_git_ops_t *)r->implementation;
    else
        printf("  [warn] resolve(GIT) 未取得实现指针\n");
    return 0;
}

/* ---- 组级 teardown：先卸插件，再删临时仓库 ---- */
static int group_teardown(void **state) {
    (void)state;
    if (g_p) {
        if (g_p->ops.stop) g_p->ops.stop(g_p);
        for (int j = 0; j < g_p->provides_count; j++)
            hw_metaproto_unregister(&g_bus.meta, g_p->provides[j], g_p->id);
        if (g_p->ops.destroy) g_p->ops.destroy(g_p);
    }
    if (g_h) dlclose(g_h);
    g_p = NULL;
    g_h = NULL;
    g_ops = NULL;
    hw_bus_shutdown(&g_bus);

    rm_tmp_tree(g_base);
    g_base[0] = g_repo[0] = g_audit[0] = '\0';
    unsetenv("HWRUN_STATE");
    return 0;
}

/* ============================================================
 * 用例
 * ============================================================ */

/* 配置加载：HWRUN_STATE/git.conf 覆盖仓库与审计路径、branch 默认 main */
static void test_git_config(void **state) {
    (void)state;
    if (!g_ops) {
        printf("  [skip] GIT 不可用\n");
        return;
    }
    git_config_t *cfg = g_ops->get_config ? g_ops->get_config() : NULL;
    assert_non_null(cfg);
    assert_string_equal(cfg->repo_path, g_repo);
    assert_string_equal(cfg->branch, "main");
    printf("  [info] repo_path=%s audit_path=%s\n", cfg->repo_path, cfg->audit_path);
}

/* 真实工作流：status/add/commit/log/status/size */
static void test_git_workflow(void **state) {
    (void)state;
    if (!g_ops) {
        printf("  [skip] GIT 不可用\n");
        return;
    }

    /* 1) git 真实可用性探测：能在临时仓库跑通一次 status 才继续 */
    char *probe = g_ops->status ? g_ops->status() : NULL;
    if (!probe) {
        printf("  [skip] 无可用 git 命令（status 失败），跳过真实仓库用例\n");
        return;
    }
    free(probe);

    /* 2) 提交身份：仅配置临时仓库（user.name/email），不影响任何全局配置 */
    {
        char cmd[640];
        snprintf(cmd, sizeof(cmd), "git -C \"%s\" config user.name 'HWRun Test'", g_repo);
        if (system(cmd) != 0) {
            printf("  [skip] git config user.name 失败\n");
            return;
        }
        snprintf(cmd, sizeof(cmd), "git -C \"%s\" config user.email 'hwrun-test@localhost'",
                 g_repo);
        if (system(cmd) != 0) {
            printf("  [skip] git config user.email 失败\n");
            return;
        }
    }

    /* 3) 写入工作区文件：内容为不可压缩的伪随机字节（xorshift），
     *    保证 compact 打包后 size-pack 至少 1KiB，size() 能稳定 > 0 */
    char fpath[512];
    snprintf(fpath, sizeof(fpath), "%s/hello.txt", g_repo);
    {
        FILE *fp = fopen(fpath, "w");
        assert_non_null(fp);
        uint32_t st = 0x12345678u;
        for (int i = 0; i < 8192; i++) {
            st ^= st << 13;
            st ^= st >> 17;
            st ^= st << 5;
            fputc((int)(st & 0xff), fp);
        }
        fputc('\n', fp);
        fclose(fp);
    }

    /* 4) 未跟踪文件出现在 status 中 */
    {
        char *st = g_ops->status();
        assert_non_null(st);
        assert_non_null(strstr(st, "hello.txt"));
        free(st);
    }

    /* 5) add + commit：返回 0 且产出 commit hash */
    assert_int_equal(g_ops->add(fpath), HWRUN_OK);
    char oid[64] = "";
    assert_int_equal(g_ops->commit("hwtest initial commit", oid, sizeof(oid)), HWRUN_OK);
    assert_true(oid[0] != '\0');
    printf("  [info] commit=%s\n", oid);

    /* 6) log(1, NULL)：非空且 hash/message 正确，随后 free_commits */
    {
        git_commit_t *head = g_ops->log(1, NULL);
        assert_non_null(head);
        assert_true(head->hash[0] != '\0');
        assert_non_null(strstr(head->message, "hwtest initial commit"));
        g_ops->free_commits(head);
    }

    /* 7) 提交后工作区干净：status 非空（可能为空串，表示 clean） */
    {
        char *st = g_ops->status();
        assert_non_null(st);
        free(st);
    }

    /* 8) size：compact(0) 真实 repack+gc 打包对象后仓库体积 > 0 */
    {
        uint64_t old_sz = 0, new_sz = 0;
        assert_int_equal(g_ops->compact(0, &old_sz, &new_sz), HWRUN_OK);
        uint64_t sz = g_ops->size();
        assert_true(sz > 0);
        printf("  [info] repo size=%llu bytes\n", (unsigned long long)sz);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_git_config),
        cmocka_unit_test(test_git_workflow),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
