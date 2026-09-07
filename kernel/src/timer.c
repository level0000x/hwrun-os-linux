#include "kernel.h"
#include "timer.h"
#include "arch/x86.h"
#define PIT_FREQUENCY 1193180
static uint64_t ticks;
void timer_init(uint32_t frequency) { uint16_t divisor = (uint16_t)(PIT_FREQUENCY / (frequency ? frequency : 1)); outb(0x43, 0x36); outb(0x40, divisor & 0xff); outb(0x40, divisor >> 8); }
void timer_handler(void) { ticks++; if ((ticks % 10) == 0) yield(); }
uint64_t timer_get_ticks(void) { return ticks; }
