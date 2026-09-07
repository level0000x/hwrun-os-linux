#include "kernel.h"
#include "ipc.h"
#include "task.h"
#include "mm.h"
#include "string.h"

struct ipc_msg *msg_queue;

static struct ipc_msg *take_message(uint32_t target) {
    struct ipc_msg *previous = NULL;
    struct ipc_msg *message = msg_queue;
    while (message) {
        if (message->to == target) {
            if (previous) previous->next = message->next;
            else msg_queue = message->next;
            return message;
        }
        previous = message;
        message = message->next;
    }
    return NULL;
}

int ipc_send(uint32_t target_tid, const void *data, size_t len) {
    if (!current_task || !data || len > IPC_MAX_DATA) return -1;
    if (target_tid == current_task->id) return -4;
    if (!task_find(target_tid)) return -2;
    struct ipc_msg *message = kmalloc(sizeof(*message));
    if (!message) return -3;
    message->from = task_get_id(); message->to = target_tid; message->type = 0;
    message->len = len; message->reply = NULL; message->next = NULL;
    memcpy(message->data, data, len);
    struct ipc_msg **tail = &msg_queue;
    while (*tail) tail = &(*tail)->next;
    *tail = message;
    struct task *target = task_find(target_tid);
    current_task->state = TASK_STATE_BLOCKED;
    current_task->waiting_for = target;
    if (target->state == TASK_STATE_BLOCKED) {
        target->state = TASK_STATE_READY;
        target->waiting_for = NULL;
    }
    yield();
    return 0;
}

struct ipc_msg *ipc_recv(void) {
    struct ipc_msg *message;
    while (!(message = take_message(task_get_id()))) {
        current_task->state = TASK_STATE_BLOCKED;
        yield();
    }
    struct task *sender = task_find(message->from);
    if (sender && sender->state == TASK_STATE_BLOCKED && sender->waiting_for == current_task) {
        sender->state = TASK_STATE_READY;
        sender->waiting_for = NULL;
    }
    return message;
}

int ipc_reply(uint32_t target_tid, const void *data, size_t len) { return ipc_send(target_tid, data, len); }
int ipc_try_recv(struct ipc_msg **out) { if (!out) return -4; *out = take_message(task_get_id()); return *out ? 0 : -1; }
