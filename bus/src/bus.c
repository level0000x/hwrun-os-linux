/*
 * bus.c — 组件总线主体实现
 *
 * 系统的插件管理器：扫描插件目录、装载插件、按依赖链依序启动、协议注册。
 */

#include "bus.h"

#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

static int  check_add_path(hw_bus_t *bus, const char *path);
static int  scan_dir(hw_bus_t *bus, const char *dir);
static int  scan_dir_rec(hw_bus_t *bus, const char *base, const char *sub);
static char **copy_str_arr(char *const *src, int count);

int hw_bus_init(hw_bus_t *bus, const char *state_dir, const char *scan_path,
                int level) {
    if (!bus) return HWRUN_EINVAL;
    memset(bus, 0, sizeof(*bus));

    if (state_dir) snprintf(bus->state_dir, sizeof(bus->state_dir), "%s", state_dir);
    else           snprintf(bus->state_dir, sizeof(bus->state_dir), "/var/lib/hwrun");

    hw_log_init(&bus->log, NULL, level);
    hw_param_init(&bus->params, bus->state_dir);
    hw_metaproto_init(&bus->meta, bus->state_dir);

    /* 默认扫描路径 */
    if (scan_path) {
        check_add_path(bus, scan_path);
    } else {
        check_add_path(bus, "/usr/lib/hwrun/plugins");
        check_add_path(bus, "/etc/hwrun/plugins");
    }

    bus->auto_install_deps = 1;
    bus->initialized = 1;

    /* 注册 BUS 内置子系统到协议表，作为插件依赖可解析的前置协议 */
    hw_metaproto_register(&bus->meta, HWPROTO_METAPROTO, "1.0", "bus", NULL);
    hw_metaproto_register(&bus->meta, HWPROTO_BUS, "1.0", "bus", NULL);
    hw_metaproto_register(&bus->meta, HWPROTO_PARAM, "1.0", "bus", NULL);
    hw_metaproto_register(&bus->meta, HWPROTO_LOG, "1.0", "bus", NULL);
    hw_metaproto_register(&bus->meta, HWPROTO_GIT, "1.0", "bus", NULL);

    bus->running = 1;
    return HWRUN_OK;
}

void hw_bus_shutdown(hw_bus_t *bus) {
    if (!bus || !bus->initialized) return;
    /* 逆序停止所有插件 */
    for (hw_plugin_t *p = bus->plugins; p; p = p->next) {
        if (p->state == HWPLUGIN_STARTED) hw_plugin_stop(bus, p);
    }
    hw_metaproto_shutdown(&bus->meta);
    hw_param_shutdown(&bus->params);
    hw_log_shutdown(&bus->log);
    bus->initialized = 0;
    bus->running = 0;
}

static int check_add_path(hw_bus_t *bus, const char *path) {
    if (bus->scan_paths_count >= HWRUN_SCAN_PATH_MAX) return HWRUN_OK;
    snprintf(bus->scan_paths[bus->scan_paths_count],
             sizeof(bus->scan_paths[0]), "%s", path);
    bus->scan_paths_count++;
    return HWRUN_OK;
}

hw_plugin_t *hw_bus_find(hw_bus_t *bus, const char *id) {
    for (hw_plugin_t *p = bus->plugins; p; p = p->next)
        if (hw_str_eq(p->id, id)) return p;
    return NULL;
}

int hw_bus_plugin_count(hw_bus_t *bus) {
    int n = 0;
    for (hw_plugin_t *p = bus->plugins; p; p = p->next) n++;
    return n;
}

/* 复制字符串数组（避免指向栈数组） */
static char **copy_str_arr(char *const *src, int count) {
    if (count <= 0 || !src) return NULL;
    char **arr = calloc(count + 1, sizeof(char *));
    if (!arr) return NULL;
    for (int i = 0; i < count; i++) {
        arr[i] = src[i] ? hw_strdup(src[i]) : NULL;
    }
    return arr;
}

/* 扫描一个目录：查找 plugin.yml，登记未安装插件 */
static int scan_dir(hw_bus_t *bus, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return HWRUN_OK;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        /* 用 stat 判断是否为目录（d_type 可能为 DT_UNKNOWN） */
        char sub[512];
        hw_fmt_path(sub, sizeof(sub), dir, ent->d_name);
        struct stat st;
        if (stat(sub, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) scan_dir_rec(bus, dir, ent->d_name);
    }
    closedir(d);
    return HWRUN_OK;
}

static int scan_dir_rec(hw_bus_t *bus, const char *base, const char *sub) {
    char yml_path[512];
    char plug_dir[512];
    hw_fmt_path(plug_dir, sizeof(plug_dir), base, sub);
    hw_fmt_path(yml_path, sizeof(yml_path), plug_dir, "plugin.yml");

    struct stat st;
    if (stat(yml_path, &st) != 0) return HWRUN_ENOENT;

    hw_plugin_discovery_t disc;
    int rc = hw_yml_parse_plugin(yml_path, &disc);
    if (rc != HWRUN_OK) return rc;
    if (!disc.id[0]) { hw_plugin_discovery_free(&disc); return HWRUN_EILSEQ; }

    /* 已登记则跳过 */
    if (hw_bus_find(bus, disc.id)) { hw_plugin_discovery_free(&disc); return HWRUN_EEXIST; }

    hw_plugin_t *p = calloc(1, sizeof(*p));
    if (!p) { hw_plugin_discovery_free(&disc); return HWRUN_ENOMEM; }
    snprintf(p->id, sizeof(p->id), "%s", disc.id);
    snprintf(p->name, sizeof(p->name), "%s", disc.name[0]?disc.name:disc.id);
    snprintf(p->version, sizeof(p->version), "%s", disc.version[0]?disc.version:HWRUN_VERSION);
    snprintf(p->description, sizeof(p->description), "%s", disc.description[0]?disc.description:"");
    p->type = hw_type_from_str(disc.type);
    p->state = HWPLUGIN_INSTALLED;
    snprintf(p->repo_url, sizeof(p->repo_url), "%s", disc.repo);
    snprintf(p->branch, sizeof(p->branch), "%s", disc.branch);
    snprintf(p->tag, sizeof(p->tag), "%s", disc.tag);
    snprintf(p->pre_install, sizeof(p->pre_install), "%s", disc.pre_install);
    snprintf(p->post_install, sizeof(p->post_install), "%s", disc.post_install);
    snprintf(p->pre_uninstall, sizeof(p->pre_uninstall), "%s", disc.pre_uninstall);
    snprintf(p->post_uninstall, sizeof(p->post_uninstall), "%s", disc.post_uninstall);
    p->size = disc.size;
    p->provides = copy_str_arr(disc.provides, disc.provides_count);
    p->provides_count = disc.provides_count;
    p->requires = copy_str_arr(disc.requires, disc.requires_count);
    p->requires_count = disc.requires_count;
    p->conflicts = copy_str_arr(disc.conflicts, disc.conflicts_count);
    p->conflicts_count = disc.conflicts_count;
    p->files = copy_str_arr(disc.files, disc.files_count);
    p->files_count = disc.files_count;

    /* 修正文件路径为绝对路径（相对于插件目录） */
    for (int i = 0; i < p->files_count; i++) {
        char full[512];
        if (p->files[i][0] == '/') continue;   /* 已是绝对路径 */
        hw_fmt_path(full, sizeof(full), plug_dir, p->files[i]);
        free(p->files[i]);
        p->files[i] = hw_strdup(full);
    }

    p->next = bus->plugins;
    if (bus->plugins) bus->plugins->prev = p;
    bus->plugins = p;

    HWLOG_INFOF(&bus->log, "bus", "discovered plugin: %s v%s (%s)",
                p->id, p->version, hw_type_to_str(p->type));
    return HWRUN_OK;
}

int hw_bus_scan(hw_bus_t *bus) {
    if (!bus || !bus->initialized) return HWRUN_EINVAL;
    for (int i = 0; i < bus->scan_paths_count; i++) {
        scan_dir(bus, bus->scan_paths[i]);
    }
    return HWRUN_OK;
}

int hw_bus_load(hw_bus_t *bus, const char *id) {
    hw_plugin_t *p = hw_bus_find(bus, id);
    if (!p) return HWRUN_ENOENT;
    if (p->state == HWPLUGIN_STARTED) return HWRUN_OK;
    return hw_plugin_start(bus, p);
}

/* 依赖链标准启动顺序 */
static const char *boot_order[] = {
    "metaproto", "bus", "param", "log", "git",
    "hap", "pmp", "fsp", "np", "sp", "crypto", "loader",
    "cluster", "instance", "sandbox", "input", "display",
    "audio", "power", "storage", "permission", "consensus",
    "fs_transfer", "node_discovery", "driver", "hotplug",
    "terminal", "audit", "packages", "monitor",
    "ui", "console", "desktop", "shell",
    NULL
};

int hw_bus_boot_chain(hw_bus_t *bus) {
    if (!bus || !bus->initialized) return HWRUN_EINVAL;
    hw_bus_scan(bus);

    for (int i = 0; boot_order[i]; i++) {
        hw_plugin_t *p = hw_bus_find(bus, boot_order[i]);
        if (!p) {
            HWLOG_DEBUGF(&bus->log, "bus", "boot: %s not present (skipped)",
                        boot_order[i]);
            continue;
        }
        if (p->state == HWPLUGIN_STARTED) continue;
        int rc = hw_plugin_start(bus, p);
        if (rc != HWRUN_OK) {
            HWLOG_WARNF(&bus->log, "bus", "boot: %s failed to start (rc=%d)",
                        boot_order[i], rc);
        }
    }
    return HWRUN_OK;
}

int hw_bus_resolve(hw_bus_t *bus, const char *protocol,
                   hw_protocol_route_t **out) {
    return hw_metaproto_resolve(&bus->meta, protocol, NULL, out);
}

extern int hw_bus_cli(int argc, char **argv);

int hw_bus_stop(hw_bus_t *bus, const char *id) {
    hw_plugin_t *p = hw_bus_find(bus, id);
    if (!p) return HWRUN_ENOENT;
    return hw_plugin_stop(bus, p);
}

int hw_bus_unload(hw_bus_t *bus, const char *id) {
    hw_plugin_t *p = hw_bus_find(bus, id);
    if (!p) return HWRUN_ENOENT;
    return hw_plugin_unload(bus, p);
}