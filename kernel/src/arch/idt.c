#include <stdint.h>
#include "kernel.h"
extern uint32_t idt_entries[48];
extern void idt_syscall_entry(void);
struct idt_entry { uint16_t low; uint16_t selector; uint8_t zero; uint8_t flags; uint16_t high; } __attribute__((packed));
struct idt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));
static struct idt_entry idt[256];
static struct idt_ptr pointer;
static void set_gate(uint8_t number, uintptr_t address, uint8_t flags) { idt[number].low = address; idt[number].selector = 8; idt[number].zero = 0; idt[number].flags = flags; idt[number].high = address >> 16; }
void idt_init(void) { pointer.limit = sizeof(idt) - 1; pointer.base = (uint32_t)idt; for (int i = 0; i < 48; i++) set_gate((uint8_t)i, idt_entries[i], 0x8e); set_gate(0x80, (uintptr_t)idt_syscall_entry, 0xee); __asm__ volatile("lidt %0" : : "m"(pointer)); }
