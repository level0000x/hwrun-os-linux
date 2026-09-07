#ifndef HW_IPC_H
#define HW_IPC_H
#include "kernel.h"
struct ipc_msg { uint32_t from, to, type; uint8_t data[IPC_MAX_DATA]; size_t len; struct ipc_msg *next; void *reply; };
extern struct ipc_msg *msg_queue;
#endif
