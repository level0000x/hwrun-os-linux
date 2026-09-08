/*
 * loader_run.c — 加载 / 运行 / 卸载实现
 *
 * 格式路由：
 *   - 可执行文件（ELF 本机架构、ET_EXEC/含 PT_INTERP 的 PIE，以及 shebang 脚本）
 *     用 fork + exec 真实启动，wait 时同步等待并取退出码；
 *   - ELF 共享库 (.so) 用 dlopen 驻留，供宿主链接与符号；
 *   - 非本机可执行格式（PE/Mach-O/WASM 等）返回 -ENOTSUP 不做伪执行。
 * 所有失败返回负 errno，绝不崩溃。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <dlfcn.h>

#include "hwrun.h"
#include "loader.h"

/* ---- ELF 常量（与 elf.h 一致）---- */
#define ET_EXEC 2
#define ET_DYN  3

static int loader_is_library(const loader_elf_info_t *info) {
    /* ET_DYN 且不含 PT_INTERP，通常视为位置无关的共享库 */
    return (info->e_type == ET_DYN) && !info->has_interp;
}

/* ============================================================
 * 运行脚本：解析 shebang 解释器后用 execvp 启动
 * ============================================================ */
static int run_script(const char *path, char **argv) {
    loader_format_t fmt;
    char interp[256];
    char **child_argv;
    int argc = 0, i;

    if (loader_detect_impl(path, &fmt, NULL, 0, interp, sizeof(interp)) < 0)
        return -ENOEXEC;
    if (fmt != LOADER_FORMAT_SCRIPT || interp[0] == '\0')
        return -ENOEXEC;

    /* argv = [interp, path, 用户参数...] */
    while (argv && argv[argc]) argc++;
    child_argv = calloc((size_t)argc + 3, sizeof(char *));
    if (!child_argv) return -ENOMEM;
    child_argv[0] = interp;
    child_argv[1] = (char *)path;
    for (i = 0; i < argc; i++) child_argv[i + 2] = argv[i];

    execvp(interp, child_argv);
    return -errno;   /* 仅当 exec 失败时返回 */
}

/* ============================================================
 * 运行本机 ELF 可执行文件：execvp 启动
 * ============================================================ */
static int run_exec(const char *path, char **argv, int wait, int *exit_code) {
    pid_t pid;
    int status = 0;
    int n;

    pid = fork();
    if (pid < 0) return -errno;

    if (pid == 0) {
        /* 子进程：替换为可执行程序 */
        execvp(path, argv ? argv : (char *[]){ (char *)path, NULL });
        _exit(126);   /* exec 失败 */
    }

    if (!wait) return (int)pid;   /* 后台运行，返回 PID */

    if (waitpid(pid, &status, 0) < 0) return -errno;
    if (WIFEXITED(status)) {
        n = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        n = 128 + WTERMSIG(status);
    } else {
        n = 0;
    }
    if (exit_code) *exit_code = n;
    return n;
}

/* ============================================================
 * 运行实现 <- run 协议方法
 * ============================================================ */
int loader_run_impl(const char *path, char **argv, int wait, int *exit_code) {
    loader_format_t fmt;
    loader_elf_info_t info;

    if (!path) return -EINVAL;
    if (exit_code) *exit_code = 0;

    if (loader_detect_impl(path, &fmt, NULL, 0, NULL, 0) < 0)
        return -ENOEXEC;

    switch (fmt) {
    case LOADER_FORMAT_SCRIPT:
        /* 走解释器（execvp 直接替换当前进程，不返回） */
        return run_script(path, argv);

    case LOADER_FORMAT_ELF:
        if (loader_elf_impl(path, &info) < 0)
            return -ENOEXEC;
        if (loader_is_library(&info)) {
            /* 共享库：应由 load 用 dlopen 驻留，run 不负责执行 */
            return HWRUN_ENOTSUP;
        }
        return run_exec(path, argv, wait, exit_code);

    default:
        /* PE / Mach-O / WASM / APK：本阶段不做伪执行 */
        return HWRUN_ENOTSUP;
    }
}

/* ============================================================
 * 加载实现 <- load 协议方法
 *   可执行/脚本：仅解析并填充 handle（供后续判定、信息查阅）；
 *   ELF 共享库：dlopen 真实驻留。
 * ============================================================ */
int loader_load_impl(const char *path, loader_handle_t *h) {
    loader_format_t fmt;
    loader_elf_info_t info;
    static uint64_t next_id = 1;

    if (!path || !h) return -EINVAL;
    memset(h, 0, sizeof(*h));

    if (loader_detect_impl(path, &fmt, NULL, 0,
                           h->interpreter, sizeof(h->interpreter)) < 0)
        return -ENOEXEC;

    h->id = next_id++;
    snprintf(h->path, sizeof(h->path), "%s", path);
    h->format = fmt;
    h->loaded_at = (uint64_t)time(NULL);

    switch (fmt) {
    case LOADER_FORMAT_ELF:
        if (loader_elf_impl(path, &info) < 0)
            return -ENOEXEC;
        if (loader_is_library(&info)) {
            h->dl = dlopen(path, RTLD_NOW | RTLD_LOCAL);
            if (!h->dl) {
                (void)dlerror();      /* 是真库但本机无法加载才失败 */
                return -ENOENT;
            }
            h->mode = LOADER_MODE_DLOPEN;
        } else {
            h->mode = LOADER_MODE_INFO;   /* 可执行：仅登记信息 */
        }
        return 0;

    case LOADER_FORMAT_SCRIPT:
        h->mode = LOADER_MODE_INFO;
        return 0;                          /* 脚本：登记解释器即可 */

    default:
        h->mode = LOADER_MODE_INFO;        /* 其他格式仅登记类型 */
        return 0;
    }
}

/* ============================================================
 * 卸载实现 <- unload 协议方法
 * ============================================================ */
int loader_unload_impl(loader_handle_t *h) {
    if (!h) return -EINVAL;
    if (h->mode == LOADER_MODE_DLOPEN && h->dl) {
        dlclose(h->dl);
        h->dl = NULL;
    }
    h->mode = LOADER_MODE_NONE;
    return 0;
}