/*
 * np_conn.c — NP 插件连接跟踪表实现
 *
 * 定长连接表 + 互斥锁，线程安全。NP 插件在创建/接受/连接 socket 时
 * 登记元数据，close 时注销，供上层查询。
 */

#include "np_conn.h"

#include <pthread.h>
#include <string.h>
#include <errno.h>

#define NP_CONN_CAP 512

/* 定长连接表 */
static np_socket_t s_conns[NP_CONN_CAP];
static int         s_count = 0;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

/* 找到 fd 对应的下标；未找到返回 -1 */
static int find_index(int fd) {
    for (int i = 0; i < s_count; i++) {
        if (s_conns[i].fd == fd) return i;
    }
    return -1;
}

int np_conn_register(const np_socket_t *c) {
    if (!c) return -EINVAL;
    pthread_mutex_lock(&s_lock);
    if (find_index(c->fd) >= 0) {
        /* 已存在则原地更新 */
        s_conns[find_index(c->fd)] = *c;
        pthread_mutex_unlock(&s_lock);
        return 0;
    }
    if (s_count >= NP_CONN_CAP) {
        pthread_mutex_unlock(&s_lock);
        return -ENOMEM;
    }
    s_conns[s_count++] = *c;
    pthread_mutex_unlock(&s_lock);
    return 0;
}

int np_conn_update_state(int fd, np_sock_state_t st) {
    pthread_mutex_lock(&s_lock);
    int i = find_index(fd);
    if (i < 0) { pthread_mutex_unlock(&s_lock); return -ENOENT; }
    s_conns[i].state = st;
    pthread_mutex_unlock(&s_lock);
    return 0;
}

int np_conn_set_addresses(int fd, const char *remote_ip, unsigned remote_port,
                          const char *local_ip, unsigned local_port) {
    pthread_mutex_lock(&s_lock);
    int i = find_index(fd);
    if (i < 0) { pthread_mutex_unlock(&s_lock); return -ENOENT; }
    if (remote_ip) {
        strncpy(s_conns[i].remote_ip, remote_ip, sizeof(s_conns[i].remote_ip) - 1);
        s_conns[i].remote_ip[sizeof(s_conns[i].remote_ip) - 1] = '\0';
        s_conns[i].remote_port = remote_port;
    }
    if (local_ip) {
        strncpy(s_conns[i].local_ip, local_ip, sizeof(s_conns[i].local_ip) - 1);
        s_conns[i].local_ip[sizeof(s_conns[i].local_ip) - 1] = '\0';
        s_conns[i].local_port = local_port;
    }
    pthread_mutex_unlock(&s_lock);
    return 0;
}

int np_conn_set_local(int fd, const char *local_ip, unsigned local_port) {
    return np_conn_set_addresses(fd, NULL, 0, local_ip, local_port);
}

int np_conn_unregister(int fd) {
    pthread_mutex_lock(&s_lock);
    int i = find_index(fd);
    if (i < 0) { pthread_mutex_unlock(&s_lock); return -ENOENT; }
    /* 用末尾元素覆盖，避免搬移 */
    s_conns[i] = s_conns[s_count - 1];
    s_count--;
    pthread_mutex_unlock(&s_lock);
    return 0;
}

void np_conn_clear(void) {
    pthread_mutex_lock(&s_lock);
    s_count = 0;
    pthread_mutex_unlock(&s_lock);
}

int np_conn_get(int fd, np_socket_t *out) {
    if (!out) return -EINVAL;
    pthread_mutex_lock(&s_lock);
    int i = find_index(fd);
    if (i < 0) { pthread_mutex_unlock(&s_lock); return -ENOENT; }
    *out = s_conns[i];
    pthread_mutex_unlock(&s_lock);
    return 0;
}

int np_conn_list(np_socket_t *arr, int cap, int *out_count) {
    if (!arr || !out_count) return -EINVAL;
    pthread_mutex_lock(&s_lock);
    int n = s_count < cap ? s_count : cap;
    if (n > 0) memcpy(arr, s_conns, (size_t)n * sizeof(np_socket_t));
    *out_count = n;
    pthread_mutex_unlock(&s_lock);
    return 0;
}