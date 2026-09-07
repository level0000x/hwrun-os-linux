/*
 * fsp_test.c — FSP 插件自测（可选）
 *
 * dlopen 加载 build/fsp.so，调用 hw_plugin_entry() 取得 hw_plugin_t*，
 * 通过 get_interface("FSP") 取得 hw_fsp_ops_t*，然后实测：
 *   文件 open/write/read/stat/chmod/rename/remove
 *   目录 mkdir/list/rmdir/exists、路径 normalize/join/parent、mount_list
 */
#include "hwrun.h"
#include "fsp.h"

#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("[OK]   %s\n", msg); \
    else { printf("[FAIL] %s\n", msg); failures++; } \
} while (0)

int main(void) {
    setbuf(stdout, NULL);   /* 逐行输出，便于定位崩溃点 */
    void *h = dlopen("./build/fsp.so", RTLD_NOW);
    if (!h) { printf("dlopen failed: %s\n", dlerror()); return 1; }

    hw_plugin_t *(*entry)(void) = (void *)dlsym(h, "hw_plugin_entry");
    if (!entry) { printf("no hw_plugin_entry: %s\n", dlerror()); return 1; }

    hw_plugin_t *self = entry();
    CHECK(self != NULL, "hw_plugin_entry() 返回非空");
    CHECK(strcmp(self->id, "fsp") == 0, "插件 id == fsp");
    CHECK(self->type == HWPLUGIN_TYPE_FS, "插件类型 == FS");
    CHECK(self->ops.init(self) == HWRUN_OK, "ops.init() == HWRUN_OK");

    hw_fsp_ops_t *ops = (hw_fsp_ops_t *)self->ops.get_interface("FSP");
    CHECK(ops != NULL, "get_interface(FSP) 返回非空");
    if (!ops) return 1;

    /* ---- 文件读写 ---- */
    const char *path = "./fsp_test.tmp";
    CHECK(ops->remove(path) == 0 || errno == ENOENT, "清理旧测试文件");

    int fd = ops->open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0, "open(WRONLY|CREAT)");
    const char *msg = "Hello HWRun FSP!\n";
    ssize_t w = ops->write(fd, msg, strlen(msg));
    CHECK(w == (ssize_t)strlen(msg), "write 写入全部字节");
    ops->close(fd);

    fsp_stat_t st; memset(&st, 0, sizeof(st));
    CHECK(ops->stat(path, &st) == 0, "stat 成功");
    CHECK(st.size == strlen(msg), "stat.size == 写入字节数");

    char buf[128] = {0};
    fd = ops->open(path, O_RDONLY, 0);
    ssize_t r = ops->read(fd, buf, sizeof(buf));
    ops->close(fd);
    CHECK(r == (ssize_t)strlen(msg) && strncmp(buf, msg, r) == 0, "read 回读正确");
    printf("        read content: %s", buf);

    ops->rename(path, "./fsp_test_renamed.tmp");
    CHECK(ops->access("./fsp_test_renamed.tmp", F_OK) == 0, "rename 成功");
    ops->remove("./fsp_test_renamed.tmp");

    /* ---- 目录操作 ---- */
    const char *dir = "./fsp_test_dir";
    ops->remove(dir);
    CHECK(ops->mkdir(dir, 0755) == 0, "mkdir 成功");
    CHECK(ops->exists(dir) == 1, "exists(dir) == 1");
    /* 在目录中建一个文件再 list */
    char child[512];
    ops->path_join(child, sizeof(child), dir, "a.txt");
    fd = ops->open(child, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ops->close(fd);

    fsp_dirent_t ents[8];
    int n = ops->list(dir, ents, 8);
    CHECK(n == 1, "list 返回 1 项（跳过 . 和 ..）");
    if (n == 1) CHECK(strcmp(ents[0].name, "a.txt") == 0, "list 项 == a.txt");
    ops->remove(child);
    CHECK(ops->rmdir(dir) == 0, "rmdir 成功");
    CHECK(ops->exists(dir) == 0, "exists(dir) == 0");

    /* ---- 权限 ---- */
    CHECK(ops->chmod(child, 0600) == -1 && errno == ENOENT, "chmod 不存在的文件返回 ENOENT");

    /* ---- 路径操作 ---- */
    char out[1024];
    ops->path_normalize("/a//b/./c/../d", out, sizeof(out));
    CHECK(strcmp(out, "/a/b/d") == 0, "normalize /a//b/./c/../d");
    ops->path_join(out, sizeof(out), "/tmp", "x/y");
    CHECK(strcmp(out, "/tmp/x/y") == 0, "join /tmp + x/y");
    ops->path_parent("/a/b/c", out, sizeof(out));
    CHECK(strcmp(out, "/a/b") == 0, "parent /a/b/c");

    /* ---- 挂载枚举 ---- */
    fsp_mountinfo_t mi[16];
    int mn = ops->mount_list(mi, 16);
    printf("        mount_list 返回 %d 条\n", mn);
    CHECK(mn >= 0, "mount_list 执行成功");

    dlclose(h);
    printf("\n%s (%d 失败)\n", failures ? "FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}