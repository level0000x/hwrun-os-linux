#include <stdint.h>
void enable_interrupts(void) { __asm__ volatile("sti"); }
void disable_interrupts(void) { __asm__ volatile("cli"); }
void arch_idle(void) { __asm__ volatile("hlt"); }
void arch_invlpg(uint32_t address) { __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory"); }
