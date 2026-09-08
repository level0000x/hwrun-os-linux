/*
 * hwlock.c — hw_locker 实现
 *
 * 见 hwlock.h 设计说明。hw_locker_enable_parallel() 打开前，所有
 * rd/wr/unlock 均为 no-op（单线程自举阶段零开销）；打开后进入真正
 * 的 pthread 临界区。全局开关即武装信号，进程内所有 locker 同时生效。
 */

#include "hwlock.h"

#include <errno.h>

static volatile int g_locker_armed = 0;

void hw_locker_enable_parallel(void) {
    g_locker_armed = 1;
}

int hw_locker_init(hw_locker_t *lk, hw_lock_kind_t kind) {
    if (!lk) return -EINVAL;
    lk->kind = kind;
    lk->armed = false;
    if (kind == HWLOCK_RW) {
        if (pthread_rwlock_init(&lk->rw, NULL) != 0) return -ENOMEM;
    } else {
        if (pthread_mutex_init(&lk->mtx, NULL) != 0) return -ENOMEM;
    }
    return 0;
}

void hw_locker_destroy(hw_locker_t *lk) {
    if (!lk) return;
    if (lk->kind == HWLOCK_RW)
        pthread_rwlock_destroy(&lk->rw);
    else
        pthread_mutex_destroy(&lk->mtx);
    lk->armed = false;
}

static inline bool locker_active(const hw_locker_t *lk) {
    return g_locker_armed != 0 && lk != NULL;
}

int hw_locker_rdlock(hw_locker_t *lk) {
    if (!locker_active(lk)) return 0; /* 并行开关开启前 no-op */
    int rc =
        (lk->kind == HWLOCK_RW) ? pthread_rwlock_rdlock(&lk->rw) : pthread_mutex_lock(&lk->mtx);
    return rc == 0 ? 0 : -errno;
}

int hw_locker_wrlock(hw_locker_t *lk) {
    if (!locker_active(lk)) return 0;
    int rc =
        (lk->kind == HWLOCK_RW) ? pthread_rwlock_wrlock(&lk->rw) : pthread_mutex_lock(&lk->mtx);
    return rc == 0 ? 0 : -errno;
}

int hw_locker_unlock(hw_locker_t *lk) {
    if (!locker_active(lk)) return 0;
    int rc =
        (lk->kind == HWLOCK_RW) ? pthread_rwlock_unlock(&lk->rw) : pthread_mutex_unlock(&lk->mtx);
    return rc == 0 ? 0 : -errno;
}
