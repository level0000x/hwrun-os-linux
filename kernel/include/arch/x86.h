#ifndef HW_ARCH_X86_H
#define HW_ARCH_X86_H
#include <stdint.h>
static inline uint32_t read_cr0(void) { uint32_t v; __asm__ volatile("mov %%cr0, %0" : "=r"(v)); return v; }
static inline void write_cr0(uint32_t v) { __asm__ volatile("mov %0, %%cr0" : : "r"(v)); }
static inline uint32_t read_cr3(void) { uint32_t v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v; }
static inline void write_cr3(uint32_t v) { __asm__ volatile("mov %0, %%cr3" : : "r"(v)); }
static inline void outb(uint16_t port, uint8_t value) { __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
#endif
