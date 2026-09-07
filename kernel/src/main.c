#include "kernel.h"
#include "task.h"
#include "ipc.h"
#include "console.h"
#include "loader.h"
#include "timer.h"
#include "irq.h"

static void idle_entry(void *arg) {
    (void)arg;
    for (;;) arch_idle();
}

static void bus_entry(void *arg) {
    (void)arg;
    for (;;) {
        struct ipc_msg *message = ipc_recv();
        if (message) kfree(message);
    }
}

void kmain(uint32_t magic, uint32_t addr) {
    (void)addr;
    console_init();
    printk("HWRun Microkernel 1.0\n");
    if (magic != 0x2BADB002) printk("warning: unexpected multiboot magic\n");
    mm_init();
    paging_init();
    idt_init();
    irq_init();
    timer_init(TIMER_FREQ);
    loader_init();

    idle_task = task_create("idle", idle_entry, NULL);
    current_task = idle_task;
    void *entry = loader_load("/boot/hwrun/bus.so", NULL);
    if (entry) task_create("bus", entry, NULL);
    else task_create("bus-stub", bus_entry, NULL);

    enable_interrupts();
    for (;;) yield();
}
