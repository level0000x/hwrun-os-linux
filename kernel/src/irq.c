#include "kernel.h"
#include "irq.h"
#include "timer.h"
#include "arch/x86.h"
#define PIC_MASTER_CMD 0x20
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_CMD 0xa0
#define PIC_SLAVE_DATA 0xa1
static void (*handlers[16])(void);
static void pic_remap(void) { outb(PIC_MASTER_CMD, 0x11); outb(PIC_SLAVE_CMD, 0x11); outb(PIC_MASTER_DATA, 0x20); outb(PIC_SLAVE_DATA, 0x28); outb(PIC_MASTER_DATA, 4); outb(PIC_SLAVE_DATA, 2); outb(PIC_MASTER_DATA, 1); outb(PIC_SLAVE_DATA, 1); outb(PIC_MASTER_DATA, 0); outb(PIC_SLAVE_DATA, 0); }
void register_irq_handler(uint8_t irq, void (*handler)(void)) { if (irq < 16) handlers[irq] = handler; }
void irq_handler(uint32_t vector) {
	if (vector < 32 || vector >= 48) return;
	uint32_t irq = vector - 32;
	if (handlers[irq]) handlers[irq]();
	if (irq >= 8) outb(PIC_SLAVE_CMD, 0x20);
	outb(PIC_MASTER_CMD, 0x20);
}
void exception_handler(uint32_t vector, uint32_t error) {
	disable_interrupts();
	printk("exception %u error=%x\n", vector, error);
	for (;;) arch_idle();
}
void irq_init(void) { pic_remap(); register_irq_handler(0, timer_handler); }
