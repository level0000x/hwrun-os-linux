#include "string.h"
#include <stdarg.h>
#include <stdint.h>
void *memset(void *dst, int value, size_t count) { unsigned char *p = dst; while (count--) *p++ = (unsigned char)value; return dst; }
void *memcpy(void *dst, const void *src, size_t count) { unsigned char *d = dst; const unsigned char *s = src; while (count--) *d++ = *s++; return dst; }
void *memmove(void *dst, const void *src, size_t count) { unsigned char *d = dst; const unsigned char *s = src; if (d < s) while (count--) *d++ = *s++; else { d += count; s += count; while (count--) *--d = *--s; } return dst; }
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
char *strncpy(char *dst, const char *src, size_t count) { size_t i = 0; for (; i < count && src[i]; i++) dst[i] = src[i]; for (; i < count; i++) dst[i] = 0; return dst; }
static void put_num(char *dst, size_t *pos, size_t size, uint32_t value, unsigned base) { char buf[16]; size_t n = 0; do { unsigned d = value % base; buf[n++] = d < 10 ? '0' + d : 'a' + d - 10; value /= base; } while (value); while (n && *pos + 1 < size) dst[(*pos)++] = buf[--n]; }
int vsnprintf(char *dst, size_t size, const char *fmt, va_list args) { size_t pos = 0; if (!size) return 0; while (*fmt && pos + 1 < size) { if (*fmt != '%') { dst[pos++] = *fmt++; continue; } fmt++; if (*fmt == 's') { const char *s = va_arg(args, const char *); while (*s && pos + 1 < size) dst[pos++] = *s++; } else if (*fmt == 'd' || *fmt == 'u') put_num(dst, &pos, size, va_arg(args, uint32_t), 10); else if (*fmt == 'x') put_num(dst, &pos, size, va_arg(args, uint32_t), 16); else if (*fmt == 'c') dst[pos++] = (char)va_arg(args, int); else dst[pos++] = *fmt; if (*fmt) fmt++; } dst[pos] = 0; return (int)pos; }
