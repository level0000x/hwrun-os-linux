#ifndef HW_SYSCALL_H
#define HW_SYSCALL_H
#define SYS_IPC_SEND 1
#define SYS_IPC_RECV 2
#define SYS_IPC_REPLY 3
#define SYS_YIELD 4
#define SYS_EXIT 5
#define SYS_GET_TID 6
#include <stdint.h>
uint32_t syscall_handler(uint32_t num, uint32_t a, uint32_t b, uint32_t c);
uint32_t syscall_invoke(uint32_t num, uint32_t a, uint32_t b, uint32_t c);
#endif
