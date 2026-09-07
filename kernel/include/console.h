#ifndef HW_CONSOLE_H
#define HW_CONSOLE_H
#include "kernel.h"
void console_init(void);
void console_putchar(char c);
void console_write(const char *s);
void console_write_hex(uint32_t val);
void console_write_dec(uint32_t val);
void console_set_color(uint8_t fg, uint8_t bg);
void console_clear(void);
#endif
