/*
 * bus.c — 组件总线主体实现
 *
 * 系统的插件管理器：扫描插件目录、装载插件、按依赖链依序启动、协议注册。
 */

#include "bus.h"
#include "git.h"

#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

static int check_add_path(hw_bus_t *bus, const char *path);
static int scan_dir(hw_bus_t *bus, const char *dir);
static int scan_dir_rec(hw_bus_t *bus, const char *base, const char *sub);
static char **copy_str_arr(char *const *src, int count);

/* ---- 参数变更 -> GIT 自动 commit（mark_revision 钩子） ---- */

/* mark_revision 触发：resolve GIT 协议，git 插件已启动（route 有真实实现）
 * 且 auto_commit 开启时，把参数状态文件落盘并 add + commit。
 * - git 就绪前静默跳过（记录 debug）；
 * - commit 期间若参数再次被改动而重入本回调，直接返回（防递归）。
 *   commit 不触碰参数树，实际不会重入，守卫仅为防御性。 */
static void bus_on_param_revision(void *userdata, const char *reason) {
    hw_bus_t *bus = (hw_bus_t *)userdata;
    if (!bus || !bus->initialized || !bus->running) return;
    if (bus->param_git_committing) return; /* 重入守卫 */
    bus->param_git_committing = 1;

    hw_protocol_route_t *route = NULL;
    if (hw_bus_resolve(bus, HWPROTO_GIT, &route) != HWRUN_OK || !route || !route->implementation) {
        bus->param_git_committing = 0;
        return; /* git 未就绪：静默跳过 */
    }
    hw_git_ops_t *g = (hw_git_ops_t *)route->implementation;
    if (!g->add || !g->commit) {
        bus->param_git_committing = 0;
        return;
    }

    git_config_t *cfg = g->get_config ? g->get_config() : NULL;
    if (cfg && !cfg->auto_commit) {
        bus->param_git_committing = 0;
        return;
    }

    /* 参数状态文件置于 <state>/git/（默认即 git 插件仓库目录），保证
     * git add 命中工作区；状态目录未配置时静默跳过。 */
    char state_file[512] = "";
    if (bus->params.dirs[HWPARAM_GIT][0])
        snprintf(state_file, sizeof(state_file), "%s/params.state", bus->params.dirs[HWPARAM_GIT]);
    if (!state_file[0] || hw_param_save_file(&bus->params, state_file) != HWRUN_OK) {
        HWLOG_DEBUGF(&bus->log, "bus", "param state persist skipped (git/state dir not ready)");
        bus->param_git_committing = 0;
        return;
    }

    char msg[192];
    snprintf(msg, sizeof(msg), "param: %s", (reason && reason[0]) ? reason : "tree changed");
    if (g->add(state_file) != HWRUN_OK) {
        HWLOG_DEBUGF(&bus->log, "bus", "param auto-commit add failed: %s", state_file);
    } else {
        char oid[64] = "";
        if (g->commit(msg, oid, sizeof(oid)) == HWRUN_OK)
            HWLOG_INFOF(&bus->log, "bus", "param auto-commit %s (%s)", oid, msg);
        else
            HWLOG_DEBUGF(&bus->log, "bus", "param auto-commit skipped: %s", msg);
    }
    bus->param_git_committing = 0;
}

int hw_bus_init(hw_bus_t *bus, const char *state_dir, const char *scan_path, int level) {
    if (!bus) return HWRUN_EINVAL;
    memset(bus, 0, sizeof(*bus));
    hw_locker_init(&bus->plugins_lock, HWLOCK_RW);

    if (state_dir)
        snprintf(bus->state_dir, sizeof(bus->state_dir), "%s", state_dir);
    else
        snprintf(bus->state_dir, sizeof(bus->state_dir), "/var/lib/hwrun");

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

    /* 装配参数变更钩子：param 属第 1 环不依赖 GIT，bus 在此把
     * mark_revision 事件接到 GIT 自动 commit（bus_on_param_revision）。 */
    bus->params.on_revision = bus_on_param_revision;
    bus->params.revision_userdata = bus;

    /* 绑定总线单例，供运行时注入转发 LOG/PARAM */
    hw_runtime_bus_bind(bus);

    bus->running = 1;
    return HWRUN_OK;
}

void hw_bus_shutdown(hw_bus_t *bus) {
    if (!bus || !bus->initialized) return;
    /* 逆序停止并释放所有插件节点（含 meta 与 .so 句柄）。
     * WRLOCK 只用于摘除整条链表（结构写），随后出锁再逐个 unload：
     * unload 会调用插件回调与 dlclose，属外部调用，不得持 plugins_lock。 */
    hw_plugin_t *list = NULL;
    HW_WRLOCK_GUARD(&bus->plugins_lock) {
        list = bus->plugins;
        bus->plugins = NULL;
    }
    hw_plugin_t *p = list;
    while (p) {
        hw_plugin_t *n = p->next;
        if (p->state == HWPLUGIN_STARTED || p->state == HWPLUGIN_LOADED)
            hw_plugin_unload(bus, p); /* stop + destroy + dlclose */
        hw_plugin_free_meta(p);
        free(p);
        p = n;
    }
    hw_metaproto_shutdown(&bus->meta);
    hw_param_shutdown(&bus->params);
    hw_log_shutdown(&bus->log);
    hw_locker_destroy(&bus->plugins_lock);
    bus->initialized = 0;
    bus->running = 0;
}

static int check_add_path(hw_bus_t *bus, const char *path) {
    if (bus->scan_paths_count >= HWRUN_SCAN_PATH_MAX) return HWRUN_OK;
    snprintf(bus->scan_paths[bus->scan_paths_count], sizeof(bus->scan_paths[0]), "%s", path);
    bus->scan_paths_count++;
    return HWRUN_OK;
}

hw_plugin_t *hw_bus_find(hw_bus_t *bus, const char *id) {
    if (!bus || !id) return NULL;
    hw_plugin_t *ret = NULL;
    HW_RDLOCK_GUARD(&bus->plugins_lock) {
        for (hw_plugin_t *p = bus->plugins; p; p = p->next)
            if (hw_str_eq(p->id, id)) {
                ret = p;
                break;
            }
    }
    /* 借用指针：调用方自行保证与链表增删（scan/insert）不并发竞争 */
    return ret;
}

int hw_bus_plugin_count(hw_bus_t *bus) {
    if (!bus) return 0;
    int n = 0;
    HW_RDLOCK_GUARD(&bus->plugins_lock) {
        for (hw_plugin_t *p = bus->plugins; p; p = p->next)
            n++;
    }
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
    if (!disc.id[0]) {
        hw_plugin_discovery_free(&disc);
        return HWRUN_EILSEQ;
    }

    /* 已登记则跳过 */
    if (hw_bus_find(bus, disc.id)) {
        hw_plugin_discovery_free(&disc);
        return HWRUN_EEXIST;
    }

    hw_plugin_t *p = calloc(1, sizeof(*p));
    if (!p) {
        hw_plugin_discovery_free(&disc);
        return HWRUN_ENOMEM;
    }
    snprintf(p->id, sizeof(p->id), "%s", disc.id);
    snprintf(p->name, sizeof(p->name), "%s", disc.name[0] ? disc.name : disc.id);
    snprintf(p->version, sizeof(p->version), "%s", disc.version[0] ? disc.version : HWRUN_VERSION);
    snprintf(p->description, sizeof(p->description), "%s",
             disc.description[0] ? disc.description : "");
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

    /* p 的元数据：从 disc 深拷贝出独立堆数组（含绝对路径化前的相对值），
     * 之后清空 disc 内部字符串，杜绝成功路径的 strdup 泄漏。 */
    p->provides = copy_str_arr(disc.provides, disc.provides_count);
    p->requires = copy_str_arr(disc.requires, disc.requires_count);
    p->conflicts = copy_str_arr(disc.conflicts, disc.conflicts_count);
    p->files = copy_str_arr(disc.files, disc.files_count);
    p->provides_count = disc.provides_count;
    p->requires_count = disc.requires_count;
    p->conflicts_count = disc.conflicts_count;
    p->files_count = disc.files_count;
    hw_plugin_discovery_clear(&disc); /* 释放 disc 内部 strdup（d 在栈上，不 free） */

    /* 修正文件路径为绝对路径（相对于插件目录） */
    for (int i = 0; i < p->files_count; i++) {
        if (!p->files[i]) continue;
        if (p->files[i][0] == '/') continue; /* 已是绝对路径 */
        char full[512];
        int prc = hw_fmt_path(full, sizeof(full), plug_dir, p->files[i]);
        if (prc != HWRUN_OK) {
            /* 路径过长被截断会生成错误路径，宁可不登记该文件 */
            HWLOG_WARNF(&bus->log, "bus", "discover %s: file path too long, skip: %s/%s", p->id,
                        plug_dir, p->files[i]);
            free(p->files[i]);
            p->files[i] = NULL;
            continue;
        }
        free(p->files[i]);
        p->files[i] = hw_strdup(full);
    }

    /* 链表结构写（头插）加 WRLOCK；查重经 hw_bus_find 的 RDLOCK 已先行退出，
     * 两个 guard 各自独立、不嵌套 */
    HW_WRLOCK_GUARD(&bus->plugins_lock) {
        p->next = bus->plugins;
        if (bus->plugins) bus->plugins->prev = p;
        bus->plugins = p;
    }

    HWLOG_INFOF(&bus->log, "bus", "discovered plugin: %s v%s (%s)", p->id, p->version,
                hw_type_to_str(p->type));
    return HWRUN_OK;
}

/* 释放插件节点的动态元数据（scan_dir_rec 转移来的 4 组数组） */
void hw_plugin_free_meta(hw_plugin_t *p) {
    if (!p) return;
    hw_str_list_free((char **)p->provides, p->provides_count);
    hw_str_list_free((char **)p->requires, p->requires_count);
    hw_str_list_free((char **)p->conflicts, p->conflicts_count);
    hw_str_list_free((char **)p->files, p->files_count);
    p->provides = p->requires = p->conflicts = p->files = NULL;
    p->provides_count = p->requires_count = 0;
    p->conflicts_count = p->files_count = 0;
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
    "metaproto", "bus",        "param",     "log",         "git",
    "hap",       "pmp",        "fsp",       "np",          "sp",
    "crypto",    "loader",     "compress",  "cluster",     "instance",
    "sandbox",   "input",      "display",   "audio",       "power",
    "storage",   "permission", "consensus", "fs_transfer", "node_discovery",
    "driver",    "hotplug",    "terminal",  "audit",       "packages",
    "monitor",   "ui",         "console",   "desktop",     "shell",
    NULL};

/* 插件 id 是否在 boot_order 表内（决定是否会被 boot_chain 自动启动） */
static int in_boot_order(const char *id) {
    for (int i = 0; boot_order[i]; i++)
        if (hw_str_eq(boot_order[i], id)) return 1;
    return 0;
}

int hw_bus_boot_chain(hw_bus_t *bus) {
    if (!bus || !bus->initialized) return HWRUN_EINVAL;
    hw_bus_scan(bus);

    for (int i = 0; boot_order[i]; i++) {
        hw_plugin_t *p = hw_bus_find(bus, boot_order[i]);
        if (!p) {
            HWLOG_DEBUGF(&bus->log, "bus", "boot: %s not present (skipped)", boot_order[i]);
            continue;
        }
        if (p->state == HWPLUGIN_STARTED) continue;
        int rc = hw_plugin_start(bus, p);
        if (rc != HWRUN_OK) {
            HWLOG_WARNF(&bus->log, "bus", "boot: %s failed to start (%s)", boot_order[i],
                        hw_strerror(rc));
        }
    }

    /* 启动后校验：boot_order 表外的已发现插件永远不会被自动启动，提示用户 */
    for (hw_plugin_t *p = bus->plugins; p; p = p->next) {
        if (p->state == HWPLUGIN_STARTED) continue;
        if (in_boot_order(p->id)) continue;
        HWLOG_WARNF(&bus->log, "bus",
                    "boot: %s 未纳入启动序，不会自动启动 (需要时请 plugins load %s)", p->id, p->id);
    }

    hw_bus_config_route(bus); /* 启动完成后：参数变更 -> 插件 configure 路由 */
    return HWRUN_OK;
}

int hw_bus_resolve(hw_bus_t *bus, const char *protocol, hw_protocol_route_t **out) {
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

/* ---- 参数变更 -> 插件 configure 路由 ---- */

/* param watch 回调（在 param 锁外派发，可安全调 bus 接口）。
 * key 形如 "<plugin_id>.<rest>"：首段点号前为插件 id。 */
static int config_route_cb(const char *key, const char *old_value, const char *new_value,
                           void *userdata) {
    hw_bus_t *bus = (hw_bus_t *)userdata;
    (void)old_value;
    if (!bus || !key || !new_value) return 0;

    /* 解析插件 id = key 首段（点号前） */
    const char *dot = strchr(key, '.');
    char id[64];
    size_t n = dot ? (size_t)(dot - key) : strlen(key);
    if (n == 0 || n >= sizeof(id)) return 0;
    memcpy(id, key, n);
    id[n] = '\0';

    hw_plugin_t *p = hw_bus_find(bus, id);
    if (!p || p->state != HWPLUGIN_STARTED || !p->ops.configure) return 0;

    int rc = p->ops.configure(p, key, new_value);
    if (rc != HWRUN_OK) {
        HWLOG_WARNF(&bus->log, id, "configure %s failed (%s)", key, hw_strerror(rc));
    }
    return 0;
}

int hw_bus_config_route(hw_bus_t *bus) {
    if (!bus || !bus->initialized) return HWRUN_EINVAL;
    /* 单例 watcher：pattern="" 收全部变更，回调内按 key 首段路由到插件。
     * bus 作 userdata；shutdown 时 param_shutdown 统一释放 watcher。 */
    return hw_param_watch(&bus->params, "bus", "", config_route_cb, bus);
}