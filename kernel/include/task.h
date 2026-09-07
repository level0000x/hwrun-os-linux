#ifndef HW_TASK_H
#define HW_TASK_H

#include "kernel.h"

typedef enum { TASK_STATE_READY, TASK_STATE_RUNNING, TASK_STATE_BLOCKED, TASK_STATE_ZOMBIE } task_state_t;

struct task {
    uint32_t id;
    char name[32];
    task_state_t state;
    uint8_t priority;
    void *esp;
    void (*entry)(void *);
    void *stack;
    size_t stack_size;
    void *page_dir;
    struct task *waiting_for;
    void *pending_msg;
    struct task *next;
    struct task *prev;
    uint64_t total_ticks;
};

extern struct task *current_task;
extern struct task *idle_task;
extern struct task *task_list;

#endif
