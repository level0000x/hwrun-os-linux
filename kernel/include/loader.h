#ifndef HW_LOADER_H
#define HW_LOADER_H
#include "kernel.h"
#define PLUGIN_BASE_ADDR 0x1000000
int loader_init(void);
void *loader_load(const char *path, struct task **out_task);
#endif
