#include "kernel.h"
void panic(const char *msg) { disable_interrupts(); printk("KERNEL PANIC: %s\n", msg); for (;;) __asm__ volatile("hlt"); }
