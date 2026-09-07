#ifndef HW_IRQ_H
#define HW_IRQ_H
#include "kernel.h"
void irq_init(void);
void register_irq_handler(uint8_t irq, void (*handler)(void));
void irq_handler(uint32_t irq);
void exception_handler(uint32_t vector, uint32_t error);
#endif
