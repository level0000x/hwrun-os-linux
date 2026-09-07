#include "kernel.h"
#include "console.h"
#include "string.h"
#include <stdarg.h>
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
static volatile uint16_t *const vga = (uint16_t *)0xb8000;
static uint8_t color = 0x07;
static int row, column;
void console_set_color(uint8_t fg, uint8_t bg) { color = (bg << 4) | (fg & 15); }
void console_clear(void) { for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) vga[i] = ((uint16_t)color << 8) | ' '; row = column = 0; }
void console_init(void) { console_clear(); }
void console_putchar(char ch) { if (ch == '\n') { row++; column = 0; } else { vga[row * VGA_WIDTH + column] = ((uint16_t)color << 8) | (uint8_t)ch; if (++column == VGA_WIDTH) { column = 0; row++; } } if (row == VGA_HEIGHT) { memmove((void *)vga, (const void *)(vga + VGA_WIDTH), (VGA_HEIGHT - 1) * VGA_WIDTH * 2); for (int i = 0; i < VGA_WIDTH; i++) vga[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = ((uint16_t)color << 8) | ' '; row = VGA_HEIGHT - 1; } }
void console_write(const char *s) { while (s && *s) console_putchar(*s++); }
void console_write_hex(uint32_t value) { char buffer[9]; for (int i = 7; i >= 0; i--) { uint8_t n = (value >> (i * 4)) & 15; buffer[7 - i] = n < 10 ? '0' + n : 'a' + n - 10; } buffer[8] = 0; console_write("0x"); console_write(buffer); }
void console_write_dec(uint32_t value) { char buffer[11]; int i = 0; do { buffer[i++] = '0' + value % 10; value /= 10; } while (value); while (i) console_putchar(buffer[--i]); }
void printk(const char *fmt, ...) { char buffer[256]; va_list args; va_start(args, fmt); vsnprintf(buffer, sizeof(buffer), fmt, args); va_end(args); console_write(buffer); }
void set_color(uint8_t fg, uint8_t bg) { console_set_color(fg, bg); }
void clear_screen(void) { console_clear(); }
