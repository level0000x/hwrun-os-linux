/*
 * main.c — hwrun-bus 主入口与 CLI
 *
 * 可作为 PID1（第一用户态程序）启动整条插件链，或作为命令工具使用。
 *
 *   hwrun-bus                     # PID1/默认启动全部插件
 *   hwrun plugins list            # 列出插件
 *   hwrun plugins load <id>       # 加载插件
 *   hwrun plugins stop <id>       # 停止插件
 *   hwrun params list             # 列出参数
 *   hwrun param  get <key>        # 读取参数
 *   hwrun param  set <key> <val>  # 设置参数
 *   hwrun protocols list          # 列出协议
 *   hwrun status                  # 系统状态
 */

#include "bus.h"
#include "kctl.h"

#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>

static hw_bus_t g_bus;

/* PID1 常驻：收到终止信号后优雅停机 */
static volatile sig_atomic_t g_stop = 0;
static void on_term_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static int route_count(hw_bus_t *bus);
static void hw_bus_cli_usage(const char *what);

/* ---------- status ---------- */
static int cmd_status(void) {
    hw_bus_scan(&g_bus); /* 先扫描插件再展示 */
    printf("HWRun OS %s\n", HWRUN_VERSION);
    printf("state_dir:     %s\n", g_bus.state_dir);
    printf("plugins:       %d\n", hw_bus_plugin_count(&g_bus));
    printf("protocols:     %d\n", route_count(&g_bus));
    printf("\nPlugins:\n");
    for (hw_plugin_t *p = g_bus.plugins; p; p = p->next) {
        static const char *st[] = {"uninstalled", "installed", "loaded",
                                   "started",     "stopped",   "error"};
        const char *s =
            p->state >= HWPLUGIN_UNINSTALLED && p->state <= HWPLUGIN_ERROR ? st[p->state] : "?";
        printf("  %-28s %-6s %s (%s)\n", p->id, p->version, s, hw_type_to_str(p->type));
        if (p->provides_count) {
            printf("      provides: ");
            for (int i = 0; i < p->provides_count; i++)
                printf("%s ", p->provides[i]);
            printf("\n");
        }
    }
    return HWRUN_OK;
}

static int route_count(hw_bus_t *bus) {
    hw_protocol_route_t **arr;
    int n;
    if (hw_metaproto_list(&bus->meta, &arr, &n) == HWRUN_OK) free(arr);
    return n;
}

/* ---------- plugins ---------- */
static int cmd_plugins(int argc, char **argv) {
    hw_bus_scan(&g_bus); /* 先扫描插件 */
    if (argc < 1) {
        hw_bus_scan(&g_bus);
        hw_bus_cli_usage("plugins");
        return HWRUN_OK;
    }
    const char *sub = argv[0];
    if (hw_str_eq(sub, "list")) {
        printf("%-24s %-8s %-10s %s\n", "ID", "VERSION", "STATE", "TYPE");
        for (hw_plugin_t *p = g_bus.plugins; p; p = p->next) {
            static const char *st[] = {"uninst",  "installed", "loaded",
                                       "started", "stopped",   "error"};
            const char *s = p->state <= HWPLUGIN_ERROR ? st[p->state] : "?";
            printf("%-24s %-8s %-10s %s\n", p->id, p->version, s, hw_type_to_str(p->type));
        }
        return HWRUN_OK;
    }
    if (hw_str_eq(sub, "load")) {
        if (argc < 2) return HWRUN_EINVAL;
        int rc = hw_bus_load(&g_bus, argv[1]);
        printf("load %s: %s\n", argv[1], rc == HWRUN_OK ? "ok" : "failed");
        return rc;
    }
    if (hw_str_eq(sub, "stop")) {
        if (argc < 2) return HWRUN_EINVAL;
        return hw_bus_stop(&g_bus, argv[1]);
    }
    if (hw_str_eq(sub, "remove")) {
        if (argc < 2) return HWRUN_EINVAL;
        return hw_bus_unload(&g_bus, argv[1]);
    }
    return HWRUN_EINVAL;
}

/* ---------- params ---------- */
#define PARAMS_PERSIST_FILE "/var/lib/hwrun/params/state.conf"
static void params_persist_load(void) {
    hw_param_load_file(&g_bus.params, PARAMS_PERSIST_FILE, HWPARAM_USER);
}
static void params_persist_save(void) {
    hw_param_save_file(&g_bus.params, PARAMS_PERSIST_FILE);
}
static int cmd_params(int argc, char **argv) {
    if (argc >= 1 && hw_str_eq(argv[0], "list")) {
        params_persist_load();
        hw_param_dump_all(&g_bus.params);
        return HWRUN_OK;
    }
    if (argc >= 2 && hw_str_eq(argv[0], "get")) {
        params_persist_load();
        const char *v = hw_param_get(&g_bus.params, argv[1]);
        printf("%s\n", v ? v : "(nil)");
        return HWRUN_OK;
    }
    if (argc >= 3 && hw_str_eq(argv[0], "set")) {
        params_persist_load(); /* 先加载已有参数，避免覆盖 */
        int rc = hw_param_set_value(&g_bus.params, argv[1], argv[2], HWPARAM_TYPE_STRING, NULL);
        printf("param %s=%s: %s\n", argv[1], argv[2], rc == HWRUN_OK ? "ok" : "failed");
        if (rc == HWRUN_OK) params_persist_save();
        return rc;
    }
    return HWRUN_EINVAL;
}

/* ---------- protocols ---------- */
static int cmd_protocols(void) {
    hw_protocol_route_t **arr;
    int n;
    int rc = hw_metaproto_list(&g_bus.meta, &arr, &n);
    if (rc != HWRUN_OK) {
        printf("no protocols\n");
        return HWRUN_OK;
    }
    printf("%-24s %-10s %-24s\n", "PROTOCOL", "VERSION", "PROVIDER");
    for (int i = 0; i < n; i++) {
        printf("%-24s %-10s %-24s\n", arr[i]->protocol, arr[i]->version, arr[i]->plugin_id);
    }
    free(arr);
    return HWRUN_OK;
}

/* ---------- kctl (内核边界客户端) ---------- */
static int cmd_kctl(int argc, char **argv) {
    hwrun_kctl_t c;
    int rc = hwrun_kctl_open(&c);
    if (rc != HWRUN_OK) {
        printf("kctl: no kernel boundary (/dev/hwrun): %d\n", rc);
        return rc;
    }

    if (argc < 1) { /* 默认 ping + abi */
        uint32_t abi = 0, ping = 0x12345678, orig = ping;
        rc = hwrun_kctl_get_abi(&c, &abi);
        printf("kctl: open %s: %s\n", c.device, hw_strerror(rc));
        if (rc == HWRUN_OK) printf("kctl: abi version = %u\n", abi);
        if (hwrun_kctl_ping(&c, &ping) == HWRUN_OK)
            printf("kctl: ping 0x%08x -> 0x%08x\n", orig, ping);
        hwrun_kctl_close(&c);
        return rc;
    }

    if (hw_str_eq(argv[0], "abi")) {
        uint32_t abi = 0;
        rc = hwrun_kctl_get_abi(&c, &abi);
        printf("abi: %s (%u)\n", hw_strerror(rc), abi);
    } else if (hw_str_eq(argv[0], "ping") && argc >= 2) {
        uint32_t v = (uint32_t)strtoul(argv[1], NULL, 0), orig = v;
        rc = hwrun_kctl_ping(&c, &v);
        printf("ping 0x%08x -> 0x%08x: %s\n", orig, v, hw_strerror(rc));
    } else if (hw_str_eq(argv[0], "list")) {
        const char *proto[] = {"METAPROTO", "BUS", "PARAM", "LOG",    "GIT",    "HAP", "PMP",
                               "FSP",       "NP",  "SP",    "CRYPTO", "LOADER", NULL};
        printf("%-24s %-12s %-24s\n", "PROTOCOL", "VERSION", "PROVIDER");
        for (int i = 0; proto[i]; i++) {
            hwrun_kctl_desc_t d;
            if (hwrun_kctl_protocol_resolve(&c, proto[i], &d) != HWRUN_OK) continue;
            printf("%-24s %-12s %-24s\n", d.protocol, d.version, d.provider);
        }
        rc = HWRUN_OK;
    } else if (hw_str_eq(argv[0], "resolve") && argc >= 2) {
        hwrun_kctl_desc_t d;
        rc = hwrun_kctl_protocol_resolve(&c, argv[1], &d);
        if (rc == HWRUN_OK)
            printf("%s -> v%s by %s (impl=%llu)\n", d.protocol, d.version, d.provider,
                   (unsigned long long)d.implementation);
        else
            printf("resolve %s: %s\n", argv[1], rc == -ENOENT ? "not found" : "failed");
    } else {
        rc = HWRUN_EINVAL;
        printf("usage: hwrun kctl [abi|ping <hex>|list|resolve <proto>]\n");
    }

    hwrun_kctl_close(&c);
    return rc;
}

void hw_bus_cli_usage(const char *what) {
    (void)what;
    printf("usage: hwrun plugins list|load <id>|stop <id>|remove <id>\n"
           "       hwrun params list|get <key>|set <key> <val>\n"
           "       hwrun protocols list\n"
           "       hwrun kctl [abi|ping <hex>|list|resolve <proto>]\n"
           "       hwrun status\n");
}

int hw_bus_cli(int argc, char **argv) {
    if (argc < 1) {
        hw_bus_cli_usage(NULL);
        return HWRUN_OK;
    }
    const char *cmd = argv[0];

    if (hw_str_eq(cmd, "status")) return cmd_status();
    if (hw_str_eq(cmd, "plugins")) return cmd_plugins(argc - 1, argv + 1);
    if (hw_str_eq(cmd, "params")) return cmd_params(argc - 1, argv + 1);
    if (hw_str_eq(cmd, "param")) return cmd_params(argc - 1, argv + 1);
    if (hw_str_eq(cmd, "protocols")) return cmd_protocols();
    if (hw_str_eq(cmd, "kctl")) return cmd_kctl(argc - 1, argv + 1);
    if (hw_str_eq(cmd, "scan")) return hw_bus_scan(&g_bus);
    hw_bus_cli_usage(NULL);
    return HWRUN_EINVAL;
}

int main(int argc, char **argv) {
    /* 默认启动模式：无子命令或第一个参数不是 CLI 命令时，按依赖链启动 */
    static char *cli_cmds[] = {"plugins", "params", "param", "protocols",
                               "status",  "scan",   "kctl",  NULL};
    int is_cli = 0;
    if (argc > 1) {
        for (int i = 0; cli_cmds[i]; i++)
            if (hw_str_eq(argv[1], cli_cmds[i])) {
                is_cli = 1;
                break;
            }
    }

    const char *pd = getenv("HWRUN_PLUGINS");

    int rc =
        hw_bus_init(&g_bus, getenv("HWRUN_STATE") ? getenv("HWRUN_STATE") : NULL, pd, HWLOG_INFO);
    if (rc != HWRUN_OK) {
        fprintf(stderr, "bus init failed: %d\n", rc);
        return 1;
    }

    if (is_cli) {
        setvbuf(stdout, NULL, _IONBF, 0); /* 管道下保证输出即时 */
        rc = hw_bus_cli(argc - 1, argv + 1);
        hw_bus_shutdown(&g_bus);
        return rc;
    }

    /* 默认：按依赖链启动所有插件 */
    hw_bus_boot_chain(&g_bus);
    /* 自举完成、进入并行服务期才武装锁：之前所有 hw_locker 均为 no-op，
     * 保证单线程启动阶段零开销且行为与无锁版完全一致 */
    hw_locker_enable_parallel();
    printf("HWRun OS booted (%d plugins, %d protocols)\n", hw_bus_plugin_count(&g_bus),
           route_count(&g_bus));

    /* 注册终止信号：SIGTERM/SIGINT 优雅停机；SIGPIPE 忽略（写管道崩溃防御） */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    /* PID1 常驻：pause() 挂起，信号到达返回后检查 g_stop 优雅退出 */
    while (g_bus.running && !g_stop) {
        pause();
    }
    g_bus.running = 0;
    printf("HWRun OS shutting down...\n");
    hw_bus_shutdown(&g_bus);
    return 0;
}