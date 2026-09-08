/*
 * yml.c — plugin.yml 解析器
 *
 * 解析插件描述文件为 hw_plugin_discovery_t。
 * 支持统一格式：可选 plugin: 根 + 顶层标量字段 + 列表 section
 * （provides/requires/conflicts/files/params/build）。
 *
 * 缩进感知：列表项 "- protocol: X" 后的缩进子键（如 version）以及
 * params 项内的 type/description 等子键都会被忽略，绝不回写顶层同名
 * 字段 —— 修复早期扁平状态机"子键覆盖顶层 version/type"的缺陷。
 */

#include "bus.h"

#include <string.h>
#include <ctype.h>

int hw_type_from_str(const char *s) {
    if (!s) return HWPLUGIN_TYPE_UNKNOWN;
    if (hw_str_eq(s, "kernel"))
        return HWPLUGIN_TYPE_KERNEL;
    else if (hw_str_eq(s, "libc"))
        return HWPLUGIN_TYPE_LIBC;
    else if (hw_str_eq(s, "init"))
        return HWPLUGIN_TYPE_INIT;
    else if (hw_str_eq(s, "fs"))
        return HWPLUGIN_TYPE_FS;
    else if (hw_str_eq(s, "network"))
        return HWPLUGIN_TYPE_NETWORK;
    else if (hw_str_eq(s, "security"))
        return HWPLUGIN_TYPE_SECURITY;
    else if (hw_str_eq(s, "storage"))
        return HWPLUGIN_TYPE_STORAGE;
    else if (hw_str_eq(s, "logging"))
        return HWPLUGIN_TYPE_LOGGING;
    else if (hw_str_eq(s, "scheduler"))
        return HWPLUGIN_TYPE_SCHEDULER;
    else if (hw_str_eq(s, "git"))
        return HWPLUGIN_TYPE_GIT;
    else if (hw_str_eq(s, "loader"))
        return HWPLUGIN_TYPE_LOADER;
    else if (hw_str_eq(s, "ui"))
        return HWPLUGIN_TYPE_UI;
    else if (hw_str_eq(s, "tools"))
        return HWPLUGIN_TYPE_TOOLS;
    else if (hw_str_eq(s, "management"))
        return HWPLUGIN_TYPE_MANAGEMENT;
    else if (hw_str_eq(s, "driver"))
        return HWPLUGIN_TYPE_DRIVER;
    else if (hw_str_eq(s, "container"))
        return HWPLUGIN_TYPE_CONTAINER;
    else if (hw_str_eq(s, "crypto"))
        return HWPLUGIN_TYPE_CRYPTO;
    else if (hw_str_eq(s, "application"))
        return HWPLUGIN_TYPE_MANAGEMENT;
    return HWPLUGIN_TYPE_UNKNOWN;
}

const char *hw_type_to_str(int type) {
    switch (type) {
    case HWPLUGIN_TYPE_KERNEL:
        return "kernel";
    case HWPLUGIN_TYPE_LIBC:
        return "libc";
    case HWPLUGIN_TYPE_INIT:
        return "init";
    case HWPLUGIN_TYPE_FS:
        return "fs";
    case HWPLUGIN_TYPE_NETWORK:
        return "network";
    case HWPLUGIN_TYPE_SECURITY:
        return "security";
    case HWPLUGIN_TYPE_STORAGE:
        return "storage";
    case HWPLUGIN_TYPE_LOGGING:
        return "logging";
    case HWPLUGIN_TYPE_SCHEDULER:
        return "scheduler";
    case HWPLUGIN_TYPE_GIT:
        return "git";
    case HWPLUGIN_TYPE_LOADER:
        return "loader";
    case HWPLUGIN_TYPE_UI:
        return "ui";
    case HWPLUGIN_TYPE_TOOLS:
        return "tools";
    case HWPLUGIN_TYPE_MANAGEMENT:
        return "management";
    case HWPLUGIN_TYPE_DRIVER:
        return "driver";
    case HWPLUGIN_TYPE_CONTAINER:
        return "container";
    case HWPLUGIN_TYPE_CRYPTO:
        return "crypto";
    default:
        return "unknown";
    }
}

/* 去首尾空白 */
static char *trim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        e--;
    *e = '\0';
    return s;
}

/* 去引号 */
static char *unquote(char *s) {
    size_t n = strlen(s);
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') || (s[0] == '\'' && s[n - 1] == '\''))) {
        s[n - 1] = '\0';
        return s + 1;
    }
    return s;
}

static void set_str(char *dst, size_t cap, const char *val) {
    snprintf(dst, cap, "%s", unquote(trim((char *)val)));
}

static int add_str(char **arr, int arr_max, int *count, const char *val) {
    if (*count >= arr_max) return HWRUN_ENOMEM;
    const char *v = unquote(trim((char *)val));
    if (!*v) return HWRUN_OK; /* 空项（如 conflicts 的 "" 占位）不入列 */
    char *dup = hw_strdup(v);
    if (!dup) return HWRUN_ENOMEM;
    arr[(*count)++] = dup;
    return HWRUN_OK;
}

/* 释放解析结果内部的动态字符串数组。
 * 注意：d 本身通常位于调用方栈上（bus.c scan_dir_rec），本函数只清内部，
 * 不 free(d)。若 d 由堆分配，调用方自行释放。 */
void hw_plugin_discovery_clear(hw_plugin_discovery_t *d) {
    if (!d) return;
    for (int i = 0; i < d->provides_count; i++)
        free(d->provides[i]);
    for (int i = 0; i < d->requires_count; i++)
        free(d->requires[i]);
    for (int i = 0; i < d->conflicts_count; i++)
        free(d->conflicts[i]);
    for (int i = 0; i < d->files_count; i++)
        free(d->files[i]);
    memset(d, 0, sizeof(*d));
}

/* 兼容旧名：仅清内部（含堆分配的 discovery 对象请另行 free） */
void hw_plugin_discovery_free(hw_plugin_discovery_t *d) {
    hw_plugin_discovery_clear(d);
}

/* ---- 缩进感知的状态机内部定义 ---- */

/* 顶层标量赋值（仅在"不在任何列表项/子键内"时命中） */
static void set_top_field(hw_plugin_discovery_t *d, const char *key, const char *val) {
    if (hw_str_eq(key, "id"))
        set_str(d->id, sizeof(d->id), val);
    else if (hw_str_eq(key, "name"))
        set_str(d->name, sizeof(d->name), val);
    else if (hw_str_eq(key, "version"))
        set_str(d->version, sizeof(d->version), val);
    else if (hw_str_eq(key, "type"))
        set_str(d->type, sizeof(d->type), val);
    else if (hw_str_eq(key, "description"))
        set_str(d->description, sizeof(d->description), val);
    else if (hw_str_eq(key, "repo"))
        set_str(d->repo, sizeof(d->repo), val);
    else if (hw_str_eq(key, "branch"))
        set_str(d->branch, sizeof(d->branch), val);
    else if (hw_str_eq(key, "tag"))
        set_str(d->tag, sizeof(d->tag), val);
    else if (hw_str_eq(key, "license"))
        set_str(d->license, sizeof(d->license), val);
    else if (hw_str_eq(key, "author"))
        set_str(d->author, sizeof(d->author), val);
    else if (hw_str_eq(key, "url"))
        set_str(d->url, sizeof(d->url), val);
    else if (hw_str_eq(key, "pre_install"))
        set_str(d->pre_install, sizeof(d->pre_install), val);
    else if (hw_str_eq(key, "post_install"))
        set_str(d->post_install, sizeof(d->post_install), val);
    else if (hw_str_eq(key, "pre_uninstall"))
        set_str(d->pre_uninstall, sizeof(d->pre_uninstall), val);
    else if (hw_str_eq(key, "post_uninstall"))
        set_str(d->post_uninstall, sizeof(d->post_uninstall), val);
    else if (hw_str_eq(key, "size"))
        d->size = (size_t)strtoull(val, NULL, 10);
}

/* 协议字符串列表的四种 section 名 */
static bool is_proto_section(const char *s) {
    return hw_str_eq(s, "provides") || hw_str_eq(s, "requires") || hw_str_eq(s, "conflicts") ||
           hw_str_eq(s, "files");
}

int hw_yml_parse_plugin(const char *yml_path, hw_plugin_discovery_t *d) {
    if (!yml_path || !d) return HWRUN_EINVAL;
    memset(d, 0, sizeof(*d));

    FILE *fp = fopen(yml_path, "r");
    if (!fp) return HWRUN_ENOENT;

    char line[1024];
    /*
     * state：
     *   section      — 当前协议/参数列表名（provides/requires/conflicts/files/
     *                   params/build/...；"" 表示在顶层）
     *   sec_indent   — 该 section 头的缩进
     *   顶层标量只在 indent <= sec_indent（即回到 section 同级）且
     *   非 "- " 列表项、非列表项子键时赋值。
     */
    char section[64] = "";
    int sec_indent = -1;

    while (fgets(line, sizeof(line), fp)) {
        /* 计算缩进（仅空格；制表符按 1 计，本仓库 yml 均用空格） */
        int indent = 0;
        char *p = line;
        while (*p == ' ' || *p == '\t') {
            indent++;
            p++;
        }
        /* 去掉行尾 \n/\r，得到内容 */
        char *end = p + strlen(p);
        while (end > p && (end[-1] == '\n' || end[-1] == '\r'))
            *--end = '\0';

        /* 注释/空行 */
        char *q = p;
        while (*q == ' ' || *q == '\t')
            q++;
        if (!*q || *q == '#') continue;
        p = q;

        /* 判断当前是否"仍处于某 section 的子键区" */
        int inside_child = (section[0] != '\0') && (indent > sec_indent);

        /* 列表项："- x" / "- protocol: X" / "- key: ..." */
        if (*p == '-') {
            if (!section[0]) continue; /* 顶层裸列表无意义 */
            char *val = p + 1;
            if (*val == ' ') val++;
            val = trim(val);
            if (!*val) continue;

            if (is_proto_section(section)) {
                /* "- protocol: X" 取 protocol 值；纯字符串直接取 */
                char *c = strchr(val, ':');
                if (c) {
                    *c = '\0';
                    char *proto = trim(c + 1);
                    if (!*proto) continue;
                    if (hw_str_eq(section, "provides"))
                        add_str(d->provides, 32, &d->provides_count, proto);
                    else if (hw_str_eq(section, "requires"))
                        add_str(d->requires, 32, &d->requires_count, proto);
                    else if (hw_str_eq(section, "conflicts"))
                        add_str(d->conflicts, 16, &d->conflicts_count, proto);
                    else if (hw_str_eq(section, "files"))
                        add_str(d->files, 64, &d->files_count, proto);
                } else {
                    if (hw_str_eq(section, "provides"))
                        add_str(d->provides, 32, &d->provides_count, val);
                    else if (hw_str_eq(section, "requires"))
                        add_str(d->requires, 32, &d->requires_count, val);
                    else if (hw_str_eq(section, "conflicts"))
                        add_str(d->conflicts, 16, &d->conflicts_count, val);
                    else if (hw_str_eq(section, "files"))
                        add_str(d->files, 64, &d->files_count, val);
                }
            }
            /* params/build 等其它 section 的列表项：忽略（当前不解析） */
            continue;
        }

        /* key: value 或 key:（section 头） */
        char *sep = strchr(p, ':');
        if (!sep) continue;
        *sep = '\0';
        char *key = trim(p);
        char *val = trim(sep + 1);
        if (!*key) continue;

        /* section 头：key: 且无值（provides:/requires:/params:...）。
         * 统一 plugin.yml 中顶层列表区头与 plugin: 同级或在其子级，
         * 缩进即其内容的基线。 */
        if (!*val) {
            /* 忽略嵌套根 "plugin:"；它只是把顶层字段整体右移一层 */
            if (hw_str_eq(key, "plugin")) {
                section[0] = '\0';
                sec_indent = -1;
                continue;
            }
            snprintf(section, sizeof(section), "%s", key);
            sec_indent = indent;
            continue;
        }

        /* conflicts: [] 空列表标记：进入空列表后立即回到顶层 */
        if (hw_str_eq(key, "conflicts") && hw_str_eq(val, "[]")) {
            section[0] = '\0';
            sec_indent = -1;
            continue;
        }

        /* 顶层标量：仅在不在子键区（缩进回到 section 头或更浅）时赋值 */
        if (!inside_child) {
            set_top_field(d, key, val);
        }
        /* 否则为列表项 map 的子键（version/type/description 等），忽略，
         * 防止覆盖顶层同名字段。 */
    }

    fclose(fp);
    return HWRUN_OK;
}