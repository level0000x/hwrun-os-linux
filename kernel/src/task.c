#include "kernel.h"
#include "task.h"
#include "mm.h"
#include "paging.h"
#include "string.h"

struct task *current_task;
struct task *idle_task;
struct task *task_list;
static uint32_t next_tid = 1;

struct task *task_find(uint32_t id) {
    for (struct task *task = task_list; task; task = task->next)
        if (task->id == id) return task;
    return NULL;
}

uint32_t task_get_id(void) { return current_task ? current_task->id : 0; }

struct task *task_create(const char *name, void (*entry)(void *), void *arg) {
    struct task *task = kcalloc(1, sizeof(*task));
    if (!task) return NULL;
    task->id = next_tid++;
    task->state = TASK_STATE_READY;
    task->priority = 128;
    task->entry = entry;
    task->stack_size = KERNEL_STACK_SIZE;
    task->stack = kmalloc(task->stack_size);
    task->page_dir = create_user_page_dir();
    if (!task->stack || !task->page_dir) {
        kfree(task->stack);
        kfree(task->page_dir);
        kfree(task);
        return NULL;
    }
    strncpy(task->name, name ? name : "task", sizeof(task->name) - 1);
    task->esp = (char *)task->stack + task->stack_size;
    arch_setup_context(task, entry, arg);
    task->next = task_list;
    if (task_list) task_list->prev = task;
    task_list = task;
    return task;
}

void task_switch(struct task *next) {
    if (!next || next == current_task || next->state == TASK_STATE_ZOMBIE) return;
    struct task *previous = current_task;
    if (previous && previous->state == TASK_STATE_RUNNING) previous->state = TASK_STATE_READY;
    next->state = TASK_STATE_RUNNING;
    if (next->page_dir) switch_page_dir(next->page_dir);
    current_task = next;
    arch_context_switch(previous, next);
}

void yield(void) {
    struct task *start = current_task ? current_task : task_list;
    struct task *task = start;
    if (task) do {
        task = task->next ? task->next : task_list;
        if (task != current_task && task->state == TASK_STATE_READY) {
            task_switch(task);
            return;
        }
    } while (task && task != start);
    if (idle_task && idle_task != current_task) task_switch(idle_task);
}

void task_exit(void) {
    if (!current_task) return;
    current_task->state = TASK_STATE_ZOMBIE;
    yield();
    for (;;) arch_idle();
}
