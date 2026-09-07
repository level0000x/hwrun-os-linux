#ifndef HW_KERNEL_H
#define HW_KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define KERNEL_STACK_SIZE 8192
#define MAX_TASKS 64
#define IPC_MAX_DATA 256
#define TIMER_FREQ 1000
#define PAGE_SIZE 4096

void printk(const char *fmt, ...);
void set_color(uint8_t fg, uint8_t bg);
void clear_screen(void);
void panic(const char *msg);
void enable_interrupts(void);
void disable_interrupts(void);

struct task;
struct task *task_create(const char *name, void (*entry)(void *), void *arg);
void task_switch(struct task *next);
void yield(void);
struct task *task_find(uint32_t id);
void task_exit(void);
uint32_t task_get_id(void);

void mm_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *mm_alloc_page(void);
void mm_free_page(void *page);
void *kcalloc(size_t n, size_t size);
void *krealloc(void *ptr, size_t new_size);

void paging_init(void);
void *get_kernel_page_dir(void);
void map_page(void *pd, uint32_t virt, uint32_t phys, uint32_t flags);
void unmap_page(void *pd, uint32_t virt);
void switch_page_dir(void *pd);
void *create_user_page_dir(void);
uint32_t get_phys_addr(void *pd, uint32_t virt);

struct ipc_msg;
int ipc_send(uint32_t target_tid, const void *data, size_t len);
struct ipc_msg *ipc_recv(void);
int ipc_reply(uint32_t target_tid, const void *data, size_t len);
int ipc_try_recv(struct ipc_msg **out);

int loader_init(void);
void *loader_load(const char *path, struct task **out_task);

void irq_init(void);
void timer_init(uint32_t freq);
void register_irq_handler(uint8_t irq, void (*handler)(void));
void idt_init(void);

void arch_setup_context(struct task *t, void (*entry)(void *), void *arg);
void arch_context_switch(struct task *prev, struct task *next);
void arch_task_trampoline(void);
void arch_idle(void);
void arch_invlpg(uint32_t addr);

extern uint32_t end;

#endif
