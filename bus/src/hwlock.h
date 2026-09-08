/*
 * hwlock.h — 总线线程同步抽象
 *
 * 统一封装读写锁/互斥锁 + 作用域 RAII 宏。
 *
 * 设计约束：
 *   1. 启动期单线程（boot_chain 完成前）不应有任何锁开销 —— 提供
 *      hw_locker_enable_parallel() 全局开关：开启前 rd/wr/unlock 全 no-op。
 *   2. 禁止"持锁调用用户回调"：watcher/subscriber 通知必须
 *      "锁内快照 -> 出锁派发"，防止回调重入本系统导致自死锁。
 *
 * 用法：
 *   hw_locker_t lk;
 *   hw_locker_init(&lk, HWLOCK_RW);
 *   ...
 *   HW_RDLOCK_GUARD(&lk) { ...只读临界区... }
 *   HW_WRLOCK_GUARD(&lk) { ...写临界区... }
 *   ...
 *   hw_locker_destroy(&lk);
 */

#ifndef HWRUN_HWLOCK_H
#define HWRUN_HWLOCK_H

#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HWLOCK_RW = 0, /* 读写锁：多读单写 */
    HWLOCK_MTX,    /* 互斥锁：纯串行 */
} hw_lock_kind_t;

typedef struct hw_locker {
    pthread_rwlock_t rw; /* HWLOCK_RW 使用 */
    pthread_mutex_t mtx; /* HWLOCK_MTX 使用 */
    hw_lock_kind_t kind;
    bool armed; /* 是否已开启并行（启动期 false = no-op） */
} hw_locker_t;

/* 全局并行开关：boot_chain 完成后调用一次，此后所有锁生效 */
extern void hw_locker_enable_parallel(void);

extern int hw_locker_init(hw_locker_t *lk, hw_lock_kind_t kind);
extern void hw_locker_destroy(hw_locker_t *lk);
extern int hw_locker_rdlock(hw_locker_t *lk);
extern int hw_locker_wrlock(hw_locker_t *lk);
extern int hw_locker_unlock(hw_locker_t *lk);

/* 作用域 RAII：进入加锁，离开作用域自动解锁 */
#define HW_RDLOCK_GUARD(lk)                                                                        \
    for (int _hwlk = hw_locker_rdlock((lk)); _hwlk >= 0; hw_locker_unlock((lk)), _hwlk = -1)
#define HW_WRLOCK_GUARD(lk)                                                                        \
    for (int _hwlk = hw_locker_wrlock((lk)); _hwlk >= 0; hw_locker_unlock((lk)), _hwlk = -1)

#ifdef __cplusplus
}
#endif

#endif /* HWRUN_HWLOCK_H */
