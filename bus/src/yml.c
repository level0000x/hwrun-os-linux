/*
 * yml.c — plugin.yml 解析器
 *
 * 解析插件描述文件为 hw_plugin_discovery_t。
 * 支持 plugin.yml 的核心字段（扁平 + 简单嵌套）。
 */

#include "bus.h"

#include <string.h>
#include <ctype.h>

int hw_type_from_str(const char *s) {
    if (!s) return HWPLUGIN_TYPE_UNKNOWN;
    if      (hw_str_eq(s, "kernel"))    return HWPLUGIN_TYPE_KERNEL;
    else if (hw_str_eq(s, "libc"))      return HWPLUGIN_TYPE_LIBC;
    else if (hw_str_eq(s, "init"))      return HWPLUGIN_TYPE_INIT;
    else if (hw_str_eq(s, "fs"))        return HWPLUGIN_TYPE_FS;
    else if (hw_str_eq(s, "network"))   return HWPLUGIN_TYPE_NETWORK;
    else if (hw_str_eq(s, "security"))  return HWPLUGIN_TYPE_SECURITY;
    else if (hw_str_eq(s, "storage"))   return HWPLUGIN_TYPE_STORAGE;
    else if (hw_str_eq(s, "logging"))   return HWPLUGIN_TYPE_LOGGING;
    else if (hw_str_eq(s, "scheduler")) return HWPLUGIN_TYPE_SCHEDULER;
    else if (hw_str_eq(s, "git"))       return HWPLUGIN_TYPE_GIT;
    else if (hw_str_eq(s, "loader"))    return HWPLUGIN_TYPE_LOADER;
    else if (hw_str_eq(s, "ui"))        return HWPLUGIN_TYPE_UI;
    else if (hw_str_eq(s, "tools"))     return HWPLUGIN_TYPE_TOOLS;
    else if (hw_str_eq(s, "management"))return HWPLUGIN_TYPE_MANAGEMENT;
    else if (hw_str_eq(s, "driver"))    return HWPLUGIN_TYPE_DRIVER;
    else if (hw_str_eq(s, "container")) return HWPLUGIN_TYPE_CONTAINER;
    else if (hw_str_eq(s, "crypto"))    return HWPLUGIN_TYPE_CRYPTO;
    else if (hw_str_eq(s, "application")) return HWPLUGIN_TYPE_MANAGEMENT;
    return HWPLUGIN_TYPE_UNKNOWN;
}

const char *hw_type_to_str(int type) {
    switch (type) {
    case HWPLUGIN_TYPE_KERNEL:   return "kernel";
    case HWPLUGIN_TYPE_LIBC:     return "libc";
    case HWPLUGIN_TYPE_INIT:     return "init";
    case HWPLUGIN_TYPE_FS:       return "fs";
    case HWPLUGIN_TYPE_NETWORK:  return "network";
    case HWPLUGIN_TYPE_SECURITY: return "security";
    case HWPLUGIN_TYPE_STORAGE:  return "storage";
    case HWPLUGIN_TYPE_LOGGING:  return "logging";
    case HWPLUGIN_TYPE_SCHEDULER:return "scheduler";
    case HWPLUGIN_TYPE_GIT:      return "git";
    case HWPLUGIN_TYPE_LOADER:   return "loader";
    case HWPLUGIN_TYPE_UI:       return "ui";
    case HWPLUGIN_TYPE_TOOLS:    return "tools";
    case HWPLUGIN_TYPE_MANAGEMENT:return "management";
    case HWPLUGIN_TYPE_DRIVER:   return "driver";
    case HWPLUGIN_TYPE_CONTAINER:return "container";
    case HWPLUGIN_TYPE_CRYPTO:   return "crypto";
    default: return "unknown";
    }
}

/* 去首尾空白 */
static char *trim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) e--;
    *e = '\0';
    return s;
}

/* 去引号 */
static char *unquote(char *s) {
    size_t n = strlen(s);
    if (n >= 2 && ((s[0]=='"' && s[n-1]=='"') || (s[0]=='\'' && s[n-1]=='\''))) {
        s[n-1] = '\0';
        return s + 1;
    }
    return s;
}

static void set_str(char *dst, size_t cap, const char *val) {
    snprintf(dst, cap, "%s", unquote(trim((char*)val)));
}

static int add_str(char **arr, int arr_max, int *count, const char *val) {
    if (*count >= arr_max) return HWRUN_ENOMEM;
    char *v = hw_strdup(unquote(trim((char*)val)));
    if (!v) return HWRUN_ENOMEM;
    arr[(*count)++] = v;
    return HWRUN_OK;
}

void hw_plugin_discovery_free(hw_plugin_discovery_t *d) {
    if (!d) return;
    for (int i = 0; i < d->provides_count; i++) free(d->provides[i]);
    for (int i = 0; i < d->requires_count; i++) free(d->requires[i]);
    for (int i = 0; i < d->conflicts_count; i++) free(d->conflicts[i]);
    for (int i = 0; i < d->files_count; i++) free(d->files[i]);
    free(d);
}

int hw_yml_parse_plugin(const char *yml_path, hw_plugin_discovery_t *d) {
    if (!yml_path || !d) return HWRUN_EINVAL;
    memset(d, 0, sizeof(*d));

    FILE *fp = fopen(yml_path, "r");
    if (!fp) return HWRUN_ENOENT;

    char line[1024];
    /* 简单状态机：section 记录当前所处列表名（provides/requires/conflicts/files） */
    char section[64] = "";

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        /* 注释/空行 */
        if (!*p || *p == '#') continue;
        /* 列表项： "- xxx" 或 "  - key: value" 或 "  - protocol: value" */
        if (*p == '-') {
            char *val = p + 1;
            if (*val == ' ') val++;
            val = trim(val);
            if (*val == '\n' || *val == '\r') continue;
            /* 若形如 "key: value"，只取冒号后的值（如 "- protocol: GIT" -> "GIT"） */
            char *c = strchr(val, ':');
            if (c && *(c+1)) {
                *c = '\0';
                val = trim(c + 1);
                if (*val == '\n' || *val == '\r') continue;
            }
            if (hw_str_eq(section, "provides")) add_str(d->provides, 32, &d->provides_count, val);
            else if (hw_str_eq(section, "requires")) add_str(d->requires, 32, &d->requires_count, val);
            else if (hw_str_eq(section, "conflicts")) add_str(d->conflicts, 16, &d->conflicts_count, val);
            else if (hw_str_eq(section, "files")) add_str(d->files, 64, &d->files_count, val);
            continue;
        }

        /* key: value */
        char *sep = strchr(p, ':');
        if (!sep) continue;
        *sep = '\0';
        char *key = trim(p);
        char *val = trim(sep + 1);

        /* 判断是否 section 头 */
        if (*val == '\0') {
            /* 可能是 section 头，如 "provides:" */
            snprintf(section, sizeof(section), "%s", key);
            continue;
        }

        if (hw_str_eq(key, "id"))            set_str(d->id, sizeof(d->id), val);
        else if (hw_str_eq(key, "name"))     set_str(d->name, sizeof(d->name), val);
        else if (hw_str_eq(key, "version"))  set_str(d->version, sizeof(d->version), val);
        else if (hw_str_eq(key, "type"))     set_str(d->type, sizeof(d->type), val);
        else if (hw_str_eq(key, "description")) set_str(d->description, sizeof(d->description), val);
        else if (hw_str_eq(key, "repo"))     set_str(d->repo, sizeof(d->repo), val);
        else if (hw_str_eq(key, "branch"))   set_str(d->branch, sizeof(d->branch), val);
        else if (hw_str_eq(key, "tag"))      set_str(d->tag, sizeof(d->tag), val);
        else if (hw_str_eq(key, "license"))  set_str(d->license, sizeof(d->license), val);
        else if (hw_str_eq(key, "author"))   set_str(d->author, sizeof(d->author), val);
        else if (hw_str_eq(key, "url"))      set_str(d->url, sizeof(d->url), val);
        else if (hw_str_eq(key, "pre_install"))  set_str(d->pre_install, sizeof(d->pre_install), val);
        else if (hw_str_eq(key, "post_install")) set_str(d->post_install, sizeof(d->post_install), val);
        else if (hw_str_eq(key, "pre_uninstall")) set_str(d->pre_uninstall, sizeof(d->pre_uninstall), val);
        else if (hw_str_eq(key, "post_uninstall")) set_str(d->post_uninstall, sizeof(d->post_uninstall), val);
        else if (hw_str_eq(key, "size"))     d->size = (size_t)strtoull(val, NULL, 10);
    }

    fclose(fp);
    return HWRUN_OK;
}