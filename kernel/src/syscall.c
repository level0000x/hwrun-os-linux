#include "kernel.h"
#include "syscall.h"
#include "ipc.h"

uint32_t syscall_handler(uint32_t num, uint32_t a, uint32_t b, uint32_t c) {
    switch (num) {
    case SYS_IPC_SEND: return (uint32_t)ipc_send(a, (const void *)b, c);
    case SYS_IPC_RECV: return (uint32_t)(uintptr_t)ipc_recv();
    case SYS_IPC_REPLY: return (uint32_t)ipc_reply(a, (const void *)b, c);
    case SYS_YIELD: yield(); return 0;
    case SYS_EXIT: task_exit(); return 0;
    case SYS_GET_TID: return task_get_id();
    default: return (uint32_t)-1;
    }
}
