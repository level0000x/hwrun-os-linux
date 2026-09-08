/*
 * sp_auth.c — SP 身份认证与授权（RBAC）
 *
 * 身份认证：用户名/密码。密码绝不明文存储，采用"加盐 SHA-256"：
 *   存储内容 = 随机盐(hex) + hash_hex(SHA-256(盐字节 || 密码))
 * 认证时重算并与存储做常量时间比对，防止离线暴力与时序侧信道。
 *
 * 授权：基于角色的访问控制（RBAC）。角色->允许动作前缀表，默认拒绝。
 *   内置角色：admin(全部)、operator、viewer、security。
 *
 * 用户表持久化到 $SP_STATE_DIR/sp_users.conf（0600），支持跨进程保存；
 * 自带免泄露设计：不内置任何默认口令，必须显式 add_user 起预置。
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>

#include "sp.h"
#include "sp_internal.h"

#define SP_PASS_SALT_LEN 16
#define SP_HASH_HEX_LEN 64

/* ============================================================
 * 角色 -> 允许动作前缀（RBAC 矩阵，"*" 后缀表示前缀通配）
 * ============================================================ */
typedef struct sp_role_perms {
    const char *role;
    const char *perms[8];
} sp_role_perms_t;

static const sp_role_perms_t sp_role_matrix[] = {
    {"admin", {"*"}},
    {"operator", {"fs.write", "fs.read", "task.submit", "task.read", "key.sign", "net.config"}},
    {"viewer", {"fs.read", "task.read", "log.read"}},
    {"security", {"key.*", "user.*", "auth.*", "fs.read", "log.read"}},
    {NULL, {NULL}},
};

/* 前缀匹配：token 形如 "fs.write" / "key.*" / "*" */
static int sp_perm_match(const char *perm, const char *action) {
    if (!perm || !action) return 0;
    if (strcmp(perm, "*") == 0) return 1;
    size_t pl = strlen(perm);
    if (pl > 0 && perm[pl - 1] == '*') {
        return strncmp(perm, action, pl - 1) == 0;
    }
    return strcmp(perm, action) == 0;
}

/* 判断角色是否允许动作 */
static int sp_role_allows(const char *role, const char *action) {
    for (const sp_role_perms_t *r = sp_role_matrix; r->role; r++) {
        if (strcmp(r->role, role) != 0) continue;
        for (int i = 0; i < 8 && r->perms[i]; i++) {
            if (sp_perm_match(r->perms[i], action)) return 1;
        }
        return 0;
    }
    return 0;
}

/* ============================================================
 * 用户表查找（调用方持锁）
 * ============================================================ */
static sp_user_t *sp_user_find_locked(const char *user) {
    for (sp_user_t *u = g_sp_ctx.users; u; u = u->next)
        if (strcmp(u->user, user) == 0) return u;
    return NULL;
}

/* ============================================================
 * 密码哈希：SHA-256(盐字节 || 密码) -> hex
 * ============================================================ */
static int sp_password_hash(const char *password, const char *salt_hex, char *hash_hex,
                            size_t hash_cap) {
    uint8_t salt[SP_PASS_SALT_LEN];
    size_t slen = 0;
    if (sp_from_hex(salt_hex, salt, sizeof(salt), &slen) != SP_OK) return SP_EINVAL;

    size_t plen = strlen(password);
    size_t tot = slen + plen;
    if (tot > 4096) return SP_EINVAL;
    uint8_t buf[4096];
    memcpy(buf, salt, slen);
    memcpy(buf + slen, password, plen);

    uint8_t digest[32];
    uint32_t dlen = 32;
    if (sp_hash_compute(SP_HASH_SHA256, buf, (uint32_t)tot, digest, &dlen) != SP_OK) return SP_EIO;
    if (hash_cap < SP_HASH_HEX_LEN + 1) return SP_ENOMEM;
    sp_to_hex(digest, 32, hash_hex);
    return SP_OK;
}

/* ============================================================
 * 持久化用户表（写 0600 文件）
 * ============================================================ */
static int sp_users_save(void) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/sp_users.conf", g_sp_ctx.state_dir);

    struct stat st;
    if (stat(g_sp_ctx.state_dir, &st) != 0) mkdir(g_sp_ctx.state_dir, 0700);

    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s/sp_users.conf.tmp", g_sp_ctx.state_dir);

    FILE *f = fopen(tmp, "w");
    if (!f) return SP_EIO;
    fprintf(f, "# HWRun SP user table (salted sha256)\n");
    pthread_mutex_lock(&g_sp_ctx.lock);
    for (sp_user_t *u = g_sp_ctx.users; u; u = u->next) {
        if (strpbrk(u->user, ":\n") || strpbrk(u->roles, ":\n") || strpbrk(u->salt_hex, ":\n") ||
            strpbrk(u->hash_hex, ":\n"))
            continue; /* 跳过含分隔符的异常条目 */
        fprintf(f, "%s:%s:%s:%s:%d\n", u->user, u->salt_hex, u->hash_hex,
                u->roles[0] ? u->roles : "viewer", u->enabled);
    }
    pthread_mutex_unlock(&g_sp_ctx.lock);
    if (fclose(f) != 0) {
        unlink(tmp);
        return SP_EIO;
    }
    chmod(tmp, 0600);
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return SP_EIO;
    }
    return SP_OK;
}

/* 从持久化文件加载用户表（调用前 ctx.lock 初始化完成） */
static int sp_users_load(void) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/sp_users.conf", g_sp_ctx.state_dir);
    FILE *f = fopen(path, "r");
    if (!f) return SP_OK; /* 无文件即空表，非错误 */

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (*line == 0 || *line == '#') continue;
        /* user:salt:hash:roles:enabled */
        char *user = strtok(line, ":");
        char *salt = strtok(NULL, ":");
        char *hash = strtok(NULL, ":");
        char *roles = strtok(NULL, ":");
        char *en = strtok(NULL, ":");
        if (!user || !salt || !hash) continue;
        sp_user_t *u = calloc(1, sizeof(*u));
        if (!u) break;
        snprintf(u->user, sizeof(u->user), "%s", user);
        snprintf(u->salt_hex, sizeof(u->salt_hex), "%s", salt);
        snprintf(u->hash_hex, sizeof(u->hash_hex), "%s", hash);
        snprintf(u->roles, sizeof(u->roles), "%s", roles ? roles : "viewer");
        u->enabled = en ? atoi(en) : 1;
        u->next = g_sp_ctx.users;
        g_sp_ctx.users = u;
    }
    fclose(f);
    return SP_OK;
}

/* 决定 state_dir（SP_STATE_DIR > $HOME/.hwrun > 当前目录） */
static const char *sp_state_dir(void) {
    const char *d = getenv(SP_STATEDIR_ENV);
    if (d && *d) return d;
    const char *home = getenv("HOME");
    if (home && *home) {
        static char sbuf[512];
        snprintf(sbuf, sizeof(sbuf), "%s/.hwrun", home);
        return sbuf;
    }
    return ".";
}

/* ============================================================
 * 模块初始化/清理
 * ============================================================ */
int sp_auth_init(void) {
    snprintf(g_sp_ctx.state_dir, sizeof(g_sp_ctx.state_dir), "%s", sp_state_dir());
    return sp_users_load();
}

int sp_auth_cleanup(void) {
    sp_user_t *u = g_sp_ctx.users;
    while (u) {
        sp_user_t *t = u;
        u = u->next;
        free(t);
    }
    g_sp_ctx.users = NULL;
    return SP_OK;
}

/* ============================================================
 * 添加 / 删除用户
 * ============================================================ */
int sp_add_user(const char *user, const char *password, const char *roles, int enabled) {
    if (!user || !user[0] || !password) return SP_EINVAL;
    if (strpbrk(user, ":\n")) return SP_EINVAL;

    /* 盐 16 字节随机 */
    uint8_t salt[SP_PASS_SALT_LEN];
    if (sp_random(salt, SP_PASS_SALT_LEN) != SP_OK) return SP_EIO;
    char salt_hex[SP_PASS_SALT_LEN * 2 + 1];
    sp_to_hex(salt, SP_PASS_SALT_LEN, salt_hex);

    char hash_hex[SP_HASH_HEX_LEN + 1];
    int rc = sp_password_hash(password, salt_hex, hash_hex, sizeof(hash_hex));
    if (rc != SP_OK) return rc;

    pthread_mutex_lock(&g_sp_ctx.lock);
    if (sp_user_find_locked(user)) {
        pthread_mutex_unlock(&g_sp_ctx.lock);
        return SP_EEXIST; /* 既有用户，请 del 后重建 */
    }
    sp_user_t *u = calloc(1, sizeof(*u));
    if (!u) {
        pthread_mutex_unlock(&g_sp_ctx.lock);
        return SP_ENOMEM;
    }
    snprintf(u->user, sizeof(u->user), "%s", user);
    snprintf(u->salt_hex, sizeof(u->salt_hex), "%s", salt_hex);
    snprintf(u->hash_hex, sizeof(u->hash_hex), "%s", hash_hex);
    snprintf(u->roles, sizeof(u->roles), "%s", (roles && roles[0]) ? roles : "viewer");
    u->enabled = enabled ? 1 : 0;
    u->next = g_sp_ctx.users;
    g_sp_ctx.users = u;
    pthread_mutex_unlock(&g_sp_ctx.lock);

    return sp_users_save();
}

int sp_del_user(const char *user) {
    if (!user) return SP_EINVAL;
    pthread_mutex_lock(&g_sp_ctx.lock);
    sp_user_t **pp = &g_sp_ctx.users;
    int found = 0;
    while (*pp) {
        if (strcmp((*pp)->user, user) == 0) {
            sp_user_t *t = *pp;
            *pp = t->next;
            free(t);
            found = 1;
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&g_sp_ctx.lock);
    if (!found) return SP_ENOTFOUND;
    return sp_users_save();
}

/* ============================================================
 * 身份认证
 * ============================================================ */
int sp_authenticate(const char *user, const char *password, char *session_id, uint32_t id_cap) {
    if (!user || !password) return SP_EINVAL;

    pthread_mutex_lock(&g_sp_ctx.lock);
    sp_user_t *u = sp_user_find_locked(user);
    pthread_mutex_unlock(&g_sp_ctx.lock);
    if (!u || !u->enabled) return SP_EBADCRED;

    char calc[SP_HASH_HEX_LEN + 1];
    int rc = sp_password_hash(password, u->salt_hex, calc, sizeof(calc));
    if (rc != SP_OK) return rc;

    if (sp_ct_eq((const uint8_t *)calc, (const uint8_t *)u->hash_hex, SP_HASH_HEX_LEN) != 0)
        return SP_EBADCRED;

    if (session_id && id_cap) {
        uint8_t rnd[16];
        char hex[40];
        if (sp_random(rnd, 16) == SP_OK)
            sp_to_hex(rnd, 16, hex);
        else
            snprintf(hex, sizeof(hex), "%llu", (unsigned long long)sp_now_ms());
        snprintf(session_id, id_cap, "sess-%s-%s", hex, user);
    }
    return SP_OK;
}

/* ============================================================
 * 授权检查（RBAC）：allowed 输出是否允许
 * ============================================================ */
int sp_check_acl(const char *user, const char *action, const char *target, int *allowed) {
    (void)target;
    if (!user || !action || !allowed) return SP_EINVAL;
    *allowed = 0;

    pthread_mutex_lock(&g_sp_ctx.lock);
    sp_user_t *u = sp_user_find_locked(user);
    if (u) {
        /* 逐角色判断（逗号分隔） */
        char roles[128];
        snprintf(roles, sizeof(roles), "%s", u->roles);
        char *save = NULL;
        for (char *tok = strtok_r(roles, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            while (*tok == ' ' || *tok == '\t')
                tok++;
            if (sp_role_allows(tok, action)) {
                *allowed = 1;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_sp_ctx.lock);
    return SP_OK;
}

/* authorize：允许返回 SP_OK，否则 SP_EACCES */
int sp_authorize(const char *user, const char *action, const char *target) {
    int allowed = 0;
    int rc = sp_check_acl(user, action, target, &allowed);
    if (rc != SP_OK) return rc;
    return allowed ? SP_OK : SP_EACCES;
}

/* ============================================================
 * 回调装配
 * ============================================================ */
void sp_auth_bind(hw_sp_ops_t *ops) {
    if (!ops) return;
    ops->authenticate = sp_authenticate;
    ops->authorize = sp_authorize;
    ops->add_user = sp_add_user;
    ops->del_user = sp_del_user;
    ops->check_acl = sp_check_acl;
}