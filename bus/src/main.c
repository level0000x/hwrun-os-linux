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

#include <getopt.h>
#include <unistd.h>

static hw_bus_t g_bus;

static int route_count(hw_bus_t *bus);
static void hw_bus_cli_usage(const char *what);

/* ---------- status ---------- */
static int cmd_status(void) {
    hw_bus_scan(&g_bus);   /* 先扫描插件再展示 */
    printf("HWRun OS %s\n", HWRUN_VERSION);
    printf("state_dir:     %s\n", g_bus.state_dir);
    printf("plugins:       %d\n", hw_bus_plugin_count(&g_bus));
    printf("protocols:     %d\n", route_count(&g_bus));
    printf("\nPlugins:\n");
    for (hw_plugin_t *p = g_bus.plugins; p; p = p->next) {
        static const char *st[] = {
            "uninstalled", "installed", "loaded", "started", "stopped", "error"
        };
        const char *s = p->state >= HWPLUGIN_UNINSTALLED && p->state <= HWPLUGIN_ERROR
                        ? st[p->state] : "?";
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
    hw_protocol_route_t **arr; int n;
    if (hw_metaproto_list(&bus->meta, &arr, &n) == HWRUN_OK)
        free(arr);
    return n;
}

/* ---------- plugins ---------- */
static int cmd_plugins(int argc, char **argv) {
    hw_bus_scan(&g_bus);   /* 先扫描插件 */
    if (argc < 1) { hw_bus_scan(&g_bus); hw_bus_cli_usage("plugins"); return HWRUN_OK; }
    const char *sub = argv[0];
    if (hw_str_eq(sub, "list")) {
        printf("%-24s %-8s %-10s %s\n", "ID", "VERSION", "STATE", "TYPE");
        for (hw_plugin_t *p = g_bus.plugins; p; p = p->next) {
            static const char *st[] = {"uninst","installed","loaded","started","stopped","error"};
            const char *s = p->state <= HWPLUGIN_ERROR ? st[p->state] : "?";
            printf("%-24s %-8s %-10s %s\n", p->id, p->version, s, hw_type_to_str(p->type));
        }
        return HWRUN_OK;
    }
    if (hw_str_eq(sub, "load")) {
        if (argc < 2) return HWRUN_EINVAL;
        int rc = hw_bus_load(&g_bus, argv[1]);
        printf("load %s: %s\n", argv[1], rc==HWRUN_OK?"ok":"failed");
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
        params_persist_load();          /* 先加载已有参数，避免覆盖 */
        int rc = hw_param_set_value(&g_bus.params, argv[1], argv[2],
                                    HWPARAM_TYPE_STRING, NULL);
        printf("param %s=%s: %s\n", argv[1], argv[2], rc==HWRUN_OK?"ok":"failed");
        if (rc == HWRUN_OK) params_persist_save();
        return rc;
    }
    return HWRUN_EINVAL;
}

/* ---------- protocols ---------- */
static int cmd_protocols(void) {
    hw_protocol_route_t **arr; int n;
    int rc = hw_metaproto_list(&g_bus.meta, &arr, &n);
    if (rc != HWRUN_OK) { printf("no protocols\n"); return HWRUN_OK; }
    printf("%-24s %-10s %-24s\n", "PROTOCOL", "VERSION", "PROVIDER");
    for (int i = 0; i < n; i++) {
        printf("%-24s %-10s %-24s\n", arr[i]->protocol, arr[i]->version,
               arr[i]->plugin_id);
    }
    free(arr);
    return HWRUN_OK;
}

void hw_bus_cli_usage(const char *what) {
    (void)what;
    printf("usage: hwrun plugins list|load <id>|stop <id>|remove <id>\n"
           "       hwrun params list|get <key>|set <key> <val>\n"
           "       hwrun protocols list\n"
           "       hwrun status\n");
}

int hw_bus_cli(int argc, char **argv) {
    if (argc < 1) { hw_bus_cli_usage(NULL); return HWRUN_OK; }
    const char *cmd = argv[0];

    if (hw_str_eq(cmd, "status"))            return cmd_status();
    if (hw_str_eq(cmd, "plugins"))           return cmd_plugins(argc-1, argv+1);
    if (hw_str_eq(cmd, "params"))            return cmd_params(argc-1, argv+1);
    if (hw_str_eq(cmd, "param"))             return cmd_params(argc-1, argv+1);
    if (hw_str_eq(cmd, "protocols"))         return cmd_protocols();
    if (hw_str_eq(cmd, "scan"))              return hw_bus_scan(&g_bus);
    hw_bus_cli_usage(NULL);
    return HWRUN_EINVAL;
}

int main(int argc, char **argv) {
    /* 默认启动模式：无子命令或第一个参数不是 CLI 命令时，按依赖链启动 */
    static char *cli_cmds[] = {
        "plugins", "params", "param", "protocols", "status", "scan", NULL
    };
    int is_cli = 0;
    if (argc > 1) {
        for (int i = 0; cli_cmds[i]; i++)
            if (hw_str_eq(argv[1], cli_cmds[i])) { is_cli = 1; break; }
    }

    const char *pd = getenv("HWRUN_PLUGINS");

    int rc = hw_bus_init(&g_bus, getenv("HWRUN_STATE") ? getenv("HWRUN_STATE")
                                                     : NULL,
                         pd, HWLOG_INFO);
    if (rc != HWRUN_OK) { fprintf(stderr, "bus init failed: %d\n", rc); return 1; }

    if (is_cli) {
        setvbuf(stdout, NULL, _IONBF, 0);   /* 管道下保证输出即时 */
        rc = hw_bus_cli(argc - 1, argv + 1);
        hw_bus_shutdown(&g_bus);
        return rc;
    }

    /* 默认：按依赖链启动所有插件 */
    hw_bus_boot_chain(&g_bus);
    printf("HWRun OS booted (%d plugins, %d protocols)\n",
           hw_bus_plugin_count(&g_bus), route_count(&g_bus));

    /* PID1 模式下在此常驻；此处简单保持存活 */
    while (g_bus.running) { sleep(3600); }
    hw_bus_shutdown(&g_bus);
    return 0;
}