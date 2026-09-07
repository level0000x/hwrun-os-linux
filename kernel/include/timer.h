#ifndef HW_TIMER_H
#define HW_TIMER_H
#include "kernel.h"
void timer_init(uint32_t freq);
void timer_handler(void);
uint64_t timer_get_ticks(void);
#endif
