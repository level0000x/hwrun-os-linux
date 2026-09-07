/*
 * np_conn.h — NP 插件内部连接跟踪表
 *
 * 维护一个天然的 socket 描述符注册表，供 np_socket.c 记录
 * 每个被 NP 创建/接受/连接的 fd 的元数据（状态、地址），
 * 并向上层暴露查询/枚举接口（hw_np_ops.conn_get / conn_list）。
 */
#ifndef NP_CONN_H
#define NP_CONN_H

#include "np.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 注册一个新 socket；成功返回 0，表满返回 -ENOMEM */
int np_conn_register(const np_socket_t *c);

/* 更新某个 fd 的状态（CONNECTING/CONNECTED/LISTENING...） */
int np_conn_update_state(int fd, np_sock_state_t st);

/* 登记某个 fd 的对端与本端地址；成功返回 0 */
int np_conn_set_addresses(int fd, const char *remote_ip, unsigned remote_port,
                          const char *local_ip, unsigned local_port);

/* 更新某个 fd 的本地地址（bind 之后） */
int np_conn_set_local(int fd, const char *local_ip, unsigned local_port);

/* 注销一个 socket（close 时调用） */
int np_conn_unregister(int fd);

/* 清空整个表（destroy 时调用） */
void np_conn_clear(void);

/* 查询与枚举（即 hw_np_ops 的实现） */
int np_conn_get(int fd, np_socket_t *out);
int np_conn_list(np_socket_t *arr, int cap, int *out_count);

#ifdef __cplusplus
}
#endif

#endif /* NP_CONN_H */