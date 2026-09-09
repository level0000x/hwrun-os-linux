/*
 * permission_impl.c — PERMISSION 用户态 RBAC 真实实现
 *
 * 模型：主体(subject：用户名或进程 uid 标签) → 角色 → 权限条目。
 * 权限条目 = 动作模式(action pattern) + 资源模式(resource pattern)，
 * 二者均支持 '*'（任意串）与 '?'（单字符）通配。裁决默认拒绝：
 * 主体的任一角色有条目同时匹配动作与资源才允许（check 返回 0），
 * 否则一律拒绝（check 返回 -EACCES）。
 *
 * 存储：内存链表（角色表 + 主体-角色绑定表）+ pthread 互斥锁；
 * 每个写操作成功后自动 write-through 到状态文件，start 时自动载入，
 * 实现"重启可恢复"。状态文件是简单可读文本，格式见 state_write_locked。
 *
 * 边界（用户态 RBAC 之外的范畴不在本文件实现，见 permission.h）：
 * 用户账号/口令认证归 SP；内核 LSM/MAC/Capabilities 归薄 .ko。
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "permission.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 生命周期辅助：permission_core.c 调用（先声明，避免 -Wmissing-prototypes） */
int hw_permission_state_open(void); /* start：解析默认状态路径并载入（文件缺失视为空） */
void hw_permission_state_close(void); /* destroy：释放内存策略 */

/* ============================================================
 * 内部数据结构
 * ============================================================ */
typedef struct perm_role_node {
    char name[HW_PERMISSION_NAME_MAX];
    char description[HW_PERMISSION_DESC_MAX];
    int perm_count;
    hw_permission_perm_t perms[HW_PERMISSION_ROLE_MAX_PERMS];
    struct perm_role_node *next;
} perm_role_node_t;

typedef struct perm_binding_node {
    char subject[HW_PERMISSION_SUBJECT_MAX];
    char role[HW_PERMISSION_NAME_MAX];
    struct perm_binding_node *next;
} perm_binding_node_t;

static struct {
    perm_role_node_t *roles;
    perm_binding_node_t *bindings;
    char state_file[512]; /* 当前持久化路径；空=尚未解析 */
} g_ctx;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================
 * 基础工具
 * ============================================================ */
static char *trim_ws(char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        e--;
    *e = '\0';
    return s;
}

/* 从 *pp 取一个空白分隔 token 到 out；0=无更多 token，1=成功，-1=超长 */
static int take_tok(char **pp, char *out, size_t cap) {
    char *p = *pp;
    while (*p == ' ' || *p == '\t')
        p++;
    if (!*p) {
        *pp = p;
        return 0;
    }
    size_t n = 0;
    while (*p && *p != ' ' && *p != '\t') {
        if (n + 1 < cap) out[n] = *p;
        n++;
        p++;
    }
    *pp = p;
    if (n >= cap) {
        out[0] = '\0';
        return -1;
    }
    out[n] = '\0';
    return 1;
}

/* token（角色/主体/动作）合法性：非空、不超长、不含空白与 '#'（文件格式约束） */
static int token_ok(const char *s, size_t max) {
    if (!s || !*s) return 0;
    if (strlen(s) >= max) return 0;
    for (const char *p = s; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '#') return 0;
    }
    return 1;
}

/* 自由文本（描述/资源模式）合法性：不超长、不含换行 */
static int free_text_ok(const char *s, size_t max) {
    if (!s) return 0;
    if (strlen(s) >= max) return 0;
    for (const char *p = s; *p; p++) {
        if (*p == '\n' || *p == '\r') return 0;
    }
    return 1;
}

/* 通配匹配：'*' 任意串，'?' 单字符 */
static int glob_match(const char *pat, const char *s) {
    if (*pat == '\0') return *s == '\0';
    if (*pat == '*') {
        for (const char *p = s;; p++) {
            if (glob_match(pat + 1, p)) return 1;
            if (!*p) return 0;
        }
    }
    if (*s == '\0') return 0;
    if (*pat == '?' || *pat == *s) return glob_match(pat + 1, s + 1);
    return 0;
}

/* ============================================================
 * 链表查询 / 清空（均在持锁状态下调用）
 * ============================================================ */
static perm_role_node_t *find_role_locked(const char *name) {
    for (perm_role_node_t *r = g_ctx.roles; r; r = r->next)
        if (strcmp(r->name, name) == 0) return r;
    return NULL;
}

static perm_binding_node_t *find_binding_locked(const char *subject, const char *role) {
    for (perm_binding_node_t *b = g_ctx.bindings; b; b = b->next)
        if (strcmp(b->subject, subject) == 0 && strcmp(b->role, role) == 0) return b;
    return NULL;
}

static void clear_state_locked(void) {
    perm_role_node_t *r = g_ctx.roles;
    while (r) {
        perm_role_node_t *t = r;
        r = r->next;
        free(t);
    }
    perm_binding_node_t *b = g_ctx.bindings;
    while (b) {
        perm_binding_node_t *t = b;
        b = b->next;
        free(t);
    }
    g_ctx.roles = NULL;
    g_ctx.bindings = NULL;
}

/* 追加到表尾，保持插入序（文件/列表输出次序稳定） */
static perm_role_node_t *append_role_locked(const char *name, const char *description) {
    perm_role_node_t *n = (perm_role_node_t *)calloc(1, sizeof(*n));
    if (!n) return NULL;
    snprintf(n->name, sizeof(n->name), "%s", name);
    snprintf(n->description, sizeof(n->description), "%s", description ? description : "");
    perm_role_node_t **pp = &g_ctx.roles;
    while (*pp)
        pp = &(*pp)->next;
    *pp = n;
    return n;
}

static perm_binding_node_t *append_binding_locked(const char *subject, const char *role) {
    perm_binding_node_t *n = (perm_binding_node_t *)calloc(1, sizeof(*n));
    if (!n) return NULL;
    snprintf(n->subject, sizeof(n->subject), "%s", subject);
    snprintf(n->role, sizeof(n->role), "%s", role);
    perm_binding_node_t **pp = &g_ctx.bindings;
    while (*pp)
        pp = &(*pp)->next;
    *pp = n;
    return n;
}

/* ============================================================
 * 策略变更（均在持锁状态下调用；返回 0 或负 errno）
 * ============================================================ */
static int role_add_locked(const char *name, const char *description) {
    if (!token_ok(name, HW_PERMISSION_NAME_MAX)) return -EINVAL;
    if (!free_text_ok(description ? description : "", HW_PERMISSION_DESC_MAX)) return -EINVAL;
    if (find_role_locked(name)) return -EEXIST;
    if (!append_role_locked(name, description)) return -ENOMEM;
    return 0;
}

static int role_remove_locked(const char *name) {
    perm_role_node_t **pr = &g_ctx.roles;
    while (*pr && strcmp((*pr)->name, name) != 0)
        pr = &(*pr)->next;
    if (!*pr) return -ENOENT;
    perm_role_node_t *gone = *pr;
    *pr = gone->next;
    free(gone);
    /* 级联解除所有主体的该角色绑定 */
    perm_binding_node_t **pb = &g_ctx.bindings;
    while (*pb) {
        if (strcmp((*pb)->role, name) == 0) {
            perm_binding_node_t *b = *pb;
            *pb = b->next;
            free(b);
        } else {
            pb = &(*pb)->next;
        }
    }
    return 0;
}

static int role_add_perm_locked(const char *role, const char *action, const char *resource) {
    perm_role_node_t *r = find_role_locked(role);
    if (!r) return -ENOENT;
    if (!token_ok(action, HW_PERMISSION_ACTION_MAX)) return -EINVAL;
    const char *res = resource ? resource : "";
    if (!free_text_ok(res, HW_PERMISSION_RESOURCE_MAX)) return -EINVAL;
    for (int i = 0; i < r->perm_count; i++) {
        if (strcmp(r->perms[i].action, action) == 0 && strcmp(r->perms[i].resource, res) == 0)
            return -EEXIST;
    }
    if (r->perm_count >= HW_PERMISSION_ROLE_MAX_PERMS) return -ENOSPC;
    snprintf(r->perms[r->perm_count].action, sizeof(r->perms[r->perm_count].action), "%s", action);
    snprintf(r->perms[r->perm_count].resource, sizeof(r->perms[r->perm_count].resource), "%s", res);
    r->perm_count++;
    return 0;
}

static int role_remove_perm_locked(const char *role, const char *action, const char *resource) {
    perm_role_node_t *r = find_role_locked(role);
    if (!r) return -ENOENT;
    if (!token_ok(action, HW_PERMISSION_ACTION_MAX)) return -EINVAL;
    const char *res = resource ? resource : "";
    if (!free_text_ok(res, HW_PERMISSION_RESOURCE_MAX)) return -EINVAL;
    for (int i = 0; i < r->perm_count; i++) {
        if (strcmp(r->perms[i].action, action) == 0 && strcmp(r->perms[i].resource, res) == 0) {
            memmove(&r->perms[i], &r->perms[i + 1],
                    sizeof(r->perms[i]) * (size_t)(r->perm_count - i - 1));
            r->perm_count--;
            return 0;
        }
    }
    return -ENOENT;
}

static int grant_locked(const char *subject, const char *role) {
    if (!token_ok(subject, HW_PERMISSION_SUBJECT_MAX)) return -EINVAL;
    if (!token_ok(role, HW_PERMISSION_NAME_MAX)) return -EINVAL;
    if (!find_role_locked(role)) return -ENOENT;
    if (find_binding_locked(subject, role)) return -EEXIST;
    if (!append_binding_locked(subject, role)) return -ENOMEM;
    return 0;
}

static int revoke_locked(const char *subject, const char *role) {
    if (!token_ok(subject, HW_PERMISSION_SUBJECT_MAX)) return -EINVAL;
    if (!token_ok(role, HW_PERMISSION_NAME_MAX)) return -EINVAL;
    perm_binding_node_t **pb = &g_ctx.bindings;
    while (*pb) {
        if (strcmp((*pb)->subject, subject) == 0 && strcmp((*pb)->role, role) == 0) {
            perm_binding_node_t *b = *pb;
            *pb = b->next;
            free(b);
            return 0;
        }
        pb = &(*pb)->next;
    }
    return -ENOENT;
}

/* ============================================================
 * 状态文件读写
 *
 * 文件为 UTF-8 纯文本，每行一个指令（'#' 注释），格式：
 *   role  <角色> [描述...]                   描述=行尾剩余文本，可含空格
 *   perm  <角色> <动作模式> [资源模式...]    资源模式=行尾剩余文本，可含空格
 *   grant <主体> <角色>
 * 角色描述/资源模式中禁止换行；角色/主体/动作模式中禁止空白与 '#'。
 * ============================================================ */
static void default_state_path(char *buf, size_t cap) {
    const char *p = getenv("PERMISSION_STATE");
    if (p && *p) {
        snprintf(buf, cap, "%s", p);
        return;
    }
    const char *d = getenv("HWRUN_STATE"); /* 与 bus/main.c 状态目录约定一致 */
    if (d && *d) {
        snprintf(buf, cap, "%s/permission.state", d);
        return;
    }
    snprintf(buf, cap, "/var/lib/hwrun/permission.state");
}

static int state_write_locked(const char *path) {
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -errno;
    FILE *f = fdopen(fd, "w");
    if (!f) {
        int e = -errno;
        close(fd);
        unlink(tmp);
        return e;
    }
    fprintf(f, "# HWRun OS PERMISSION 用户态 RBAC 状态文件 v1\n");
    fprintf(f, "# 行格式: role <角色> [描述] | perm <角色> <动作模式> [资源模式] | "
               "grant <主体> <角色>\n");
    for (perm_role_node_t *r = g_ctx.roles; r; r = r->next) {
        fprintf(f, "role %s", r->name);
        if (r->description[0]) fprintf(f, " %s", r->description);
        fprintf(f, "\n");
        for (int i = 0; i < r->perm_count; i++) {
            fprintf(f, "perm %s %s", r->name, r->perms[i].action);
            if (r->perms[i].resource[0]) fprintf(f, " %s", r->perms[i].resource);
            fprintf(f, "\n");
        }
    }
    for (perm_binding_node_t *b = g_ctx.bindings; b; b = b->next)
        fprintf(f, "grant %s %s\n", b->subject, b->role);
    if (fclose(f) != 0) {
        unlink(tmp);
        return -EIO;
    }
    if (rename(tmp, path) != 0) {
        int e = -errno;
        unlink(tmp);
        return e;
    }
    return 0;
}

/* 解析单行指令；roles 已就绪后调用，apply=1 时写回内存（持锁），否则只校验语法 */
static int state_apply_line(char *line) {
    char *p = line;
    char kind[16];
    if (take_tok(&p, kind, sizeof(kind)) != 1) return -EINVAL;

    if (strcmp(kind, "role") == 0) {
        char name[HW_PERMISSION_NAME_MAX];
        if (take_tok(&p, name, sizeof(name)) != 1) return -EINVAL;
        char desc[HW_PERMISSION_DESC_MAX] = "";
        char *rest = trim_ws(p);
        if (*rest) snprintf(desc, sizeof(desc), "%s", rest);
        if (!token_ok(name, HW_PERMISSION_NAME_MAX)) return -EINVAL;
        if (!free_text_ok(desc, HW_PERMISSION_DESC_MAX)) return -EINVAL;
        /* 重复定义：静默跳过（幂等） */
        if (!find_role_locked(name)) {
            if (!append_role_locked(name, desc)) return -ENOMEM;
        }
        return 0;
    }

    if (strcmp(kind, "perm") == 0) {
        char role[HW_PERMISSION_NAME_MAX], action[HW_PERMISSION_ACTION_MAX];
        if (take_tok(&p, role, sizeof(role)) != 1) return -EINVAL;
        if (take_tok(&p, action, sizeof(action)) != 1) return -EINVAL;
        char resource[HW_PERMISSION_RESOURCE_MAX] = "";
        char *rest = trim_ws(p);
        if (*rest) snprintf(resource, sizeof(resource), "%s", rest);
        if (!find_role_locked(role)) return -ENOENT; /* 角色未定义则跳过 */
        int rc = role_add_perm_locked(role, action, resource);
        return rc == -EEXIST ? 0 : rc; /* 重复条目幂等 */
    }

    if (strcmp(kind, "grant") == 0) {
        char subject[HW_PERMISSION_SUBJECT_MAX], role[HW_PERMISSION_NAME_MAX];
        if (take_tok(&p, subject, sizeof(subject)) != 1) return -EINVAL;
        if (take_tok(&p, role, sizeof(role)) != 1) return -EINVAL;
        if (!find_role_locked(role)) return -ENOENT;
        if (find_binding_locked(subject, role)) return 0; /* 重复绑定幂等 */
        if (!append_binding_locked(subject, role)) return -ENOMEM;
        return 0;
    }

    return -EINVAL; /* 未知指令：跳过 */
}

static int state_read_locked(const char *path, int missing_ok) {
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT && missing_ok) return 0; /* 文件缺失 = 全新状态 */
        return -errno;
    }

    /* 先按角色表/绑定表的先后依赖读入全部非注释行 */
    char **lines = NULL;
    size_t n = 0, cap = 0;
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        char *t = trim_ws(buf);
        if (!*t || *t == '#') continue;
        if (n == cap) {
            size_t nc = cap ? cap * 2 : 16;
            char **nl = (char **)realloc(lines, nc * sizeof(*nl));
            if (!nl) {
                free(lines);
                fclose(f);
                return -ENOMEM;
            }
            lines = nl;
            cap = nc;
        }
        lines[n] = strdup(t);
        if (!lines[n]) {
            while (n--)
                free(lines[n]);
            free(lines);
            fclose(f);
            return -ENOMEM;
        }
        n++;
    }
    fclose(f);

    /* 替换内存策略（文件为权威快照） */
    clear_state_locked();
    int rc = 0;
    for (size_t i = 0; i < n; i++) {
        if (strncmp(lines[i], "role", 4) == 0) {
            int e = state_apply_line(lines[i]);
            if (e < 0 && e != -EINVAL) rc = e; /* 记录严重错误，坏行跳过 */
        }
    }
    for (size_t i = 0; i < n; i++) {
        if (strncmp(lines[i], "perm", 4) == 0 || strncmp(lines[i], "grant", 5) == 0)
            state_apply_line(lines[i]); /* perm/grant 跳过属可容忍（角色缺失/重复） */
    }
    for (size_t i = 0; i < n; i++)
        free(lines[i]);
    free(lines);
    return rc;
}

/* 变更后自动 write-through 到当前状态路径（持锁执行；失败仅告警，不改内存） */
static void autosave_state(void) {
    if (!g_ctx.state_file[0]) return;
    int rc;
    pthread_mutex_lock(&g_lock);
    rc = state_write_locked(g_ctx.state_file);
    pthread_mutex_unlock(&g_lock);
    if (rc != 0) HWAPI_LOGW("permission", "autosave %s 失败: %s", g_ctx.state_file, strerror(-rc));
}

/* ============================================================
 * ops 实现
 * ============================================================ */
static int32_t ops_version(void) {
    return 1;
}

static int ops_role_add(const char *name, const char *description) {
    pthread_mutex_lock(&g_lock);
    int rc = role_add_locked(name, description);
    pthread_mutex_unlock(&g_lock);
    if (rc == 0) autosave_state();
    return rc;
}

static int ops_role_remove(const char *name) {
    pthread_mutex_lock(&g_lock);
    int rc = role_remove_locked(name);
    pthread_mutex_unlock(&g_lock);
    if (rc == 0) autosave_state();
    return rc;
}

static int ops_role_add_perm(const char *role, const char *action, const char *resource) {
    pthread_mutex_lock(&g_lock);
    int rc = role_add_perm_locked(role, action, resource);
    pthread_mutex_unlock(&g_lock);
    if (rc == 0) autosave_state();
    return rc;
}

static int ops_role_remove_perm(const char *role, const char *action, const char *resource) {
    pthread_mutex_lock(&g_lock);
    int rc = role_remove_perm_locked(role, action, resource);
    pthread_mutex_unlock(&g_lock);
    if (rc == 0) autosave_state();
    return rc;
}

static int ops_role_get(const char *name, hw_permission_role_t *out) {
    if (!name || !out) return -EINVAL;
    pthread_mutex_lock(&g_lock);
    perm_role_node_t *r = find_role_locked(name);
    if (!r) {
        pthread_mutex_unlock(&g_lock);
        return -ENOENT;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", r->name);
    snprintf(out->description, sizeof(out->description), "%s", r->description);
    out->perm_count = r->perm_count;
    memcpy(out->perms, r->perms, sizeof(out->perms[0]) * (size_t)r->perm_count);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int ops_role_list(hw_permission_role_t *out, int cap, int *count) {
    if (!out || !count || cap <= 0) return -EINVAL;
    pthread_mutex_lock(&g_lock);
    int total = 0;
    for (perm_role_node_t *r = g_ctx.roles; r; r = r->next) {
        if (total < cap) {
            memset(&out[total], 0, sizeof(out[total]));
            snprintf(out[total].name, sizeof(out[total].name), "%s", r->name);
            snprintf(out[total].description, sizeof(out[total].description), "%s", r->description);
            out[total].perm_count = r->perm_count;
            memcpy(out[total].perms, r->perms, sizeof(out[total].perms[0]) * (size_t)r->perm_count);
        }
        total++;
    }
    *count = total;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int ops_grant(const char *subject, const char *role) {
    pthread_mutex_lock(&g_lock);
    int rc = grant_locked(subject, role);
    pthread_mutex_unlock(&g_lock);
    if (rc == 0) autosave_state();
    return rc;
}

static int ops_revoke(const char *subject, const char *role) {
    pthread_mutex_lock(&g_lock);
    int rc = revoke_locked(subject, role);
    pthread_mutex_unlock(&g_lock);
    if (rc == 0) autosave_state();
    return rc;
}

static int ops_binding_list(hw_permission_binding_t *out, int cap, int *count) {
    if (!out || !count || cap <= 0) return -EINVAL;
    pthread_mutex_lock(&g_lock);
    int total = 0;
    for (perm_binding_node_t *b = g_ctx.bindings; b; b = b->next) {
        if (total < cap) {
            snprintf(out[total].subject, sizeof(out[total].subject), "%s", b->subject);
            snprintf(out[total].role, sizeof(out[total].role), "%s", b->role);
        }
        total++;
    }
    *count = total;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int ops_check(const char *subject, const char *action, const char *resource) {
    if (!subject || !action || !*subject || !*action) return -EINVAL;
    const char *res = resource ? resource : "";
    pthread_mutex_lock(&g_lock);
    int allowed = 0;
    for (perm_binding_node_t *b = g_ctx.bindings; b && !allowed; b = b->next) {
        if (strcmp(b->subject, subject) != 0) continue;
        perm_role_node_t *r = find_role_locked(b->role);
        if (!r) continue; /* 悬空绑定（理论不存在）视同无权限 */
        for (int i = 0; i < r->perm_count; i++) {
            if (!glob_match(r->perms[i].action, action)) continue;
            if (r->perms[i].resource[0] && !glob_match(r->perms[i].resource, res)) continue;
            allowed = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return allowed ? 0 : -EACCES; /* 默认拒绝 */
}

static int ops_set_state_file(const char *path) {
    pthread_mutex_lock(&g_lock);
    if (!path) {
        default_state_path(g_ctx.state_file, sizeof(g_ctx.state_file));
    } else {
        if (strlen(path) >= sizeof(g_ctx.state_file)) {
            pthread_mutex_unlock(&g_lock);
            return -ENAMETOOLONG;
        }
        snprintf(g_ctx.state_file, sizeof(g_ctx.state_file), "%s", path);
    }
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int ops_save(const char *path) {
    char eff[600];
    pthread_mutex_lock(&g_lock);
    if (path) {
        snprintf(eff, sizeof(eff), "%s", path);
    } else {
        if (!g_ctx.state_file[0]) default_state_path(g_ctx.state_file, sizeof(g_ctx.state_file));
        snprintf(eff, sizeof(eff), "%s", g_ctx.state_file);
    }
    int rc = state_write_locked(eff);
    pthread_mutex_unlock(&g_lock);
    return rc;
}

static int ops_load(const char *path) {
    char eff[600];
    pthread_mutex_lock(&g_lock);
    if (path) {
        snprintf(eff, sizeof(eff), "%s", path);
    } else {
        if (!g_ctx.state_file[0]) default_state_path(g_ctx.state_file, sizeof(g_ctx.state_file));
        snprintf(eff, sizeof(eff), "%s", g_ctx.state_file);
    }
    int rc = state_read_locked(eff, 0); /* 显式 load：文件缺失返回 -ENOENT */
    pthread_mutex_unlock(&g_lock);
    return rc;
}

/* ============================================================
 * 生命周期辅助（permission_core.c 调用）
 * ============================================================ */
int hw_permission_state_open(void) {
    char path[512];
    pthread_mutex_lock(&g_lock);
    default_state_path(g_ctx.state_file, sizeof(g_ctx.state_file));
    snprintf(path, sizeof(path), "%s", g_ctx.state_file);
    int rc = state_read_locked(g_ctx.state_file, 1); /* 文件缺失 = 全新状态 */
    pthread_mutex_unlock(&g_lock);
    if (rc != 0) HWAPI_LOGW("permission", "start 载入状态 %s 异常: %s", path, strerror(-rc));
    HWAPI_LOGI("permission", "RBAC ready, state=%s", path);
    return 0;
}

void hw_permission_state_close(void) {
    pthread_mutex_lock(&g_lock);
    clear_state_locked();
    g_ctx.state_file[0] = '\0';
    pthread_mutex_unlock(&g_lock);
}

/* ops 表：get_interface("PERMISSION") 返回 */
hw_permission_ops_t hw_permission_ops = {
    .version = ops_version,
    .role_add = ops_role_add,
    .role_remove = ops_role_remove,
    .role_add_perm = ops_role_add_perm,
    .role_remove_perm = ops_role_remove_perm,
    .role_get = ops_role_get,
    .role_list = ops_role_list,
    .grant = ops_grant,
    .revoke = ops_revoke,
    .binding_list = ops_binding_list,
    .check = ops_check,
    .set_state_file = ops_set_state_file,
    .save = ops_save,
    .load = ops_load,
};

#ifdef __cplusplus
}
#endif
