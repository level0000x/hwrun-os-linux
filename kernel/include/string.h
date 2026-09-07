#ifndef HW_STRING_H
#define HW_STRING_H
#include <stddef.h>
void *memset(void *dst, int value, size_t count);
void *memcpy(void *dst, const void *src, size_t count);
void *memmove(void *dst, const void *src, size_t count);
size_t strlen(const char *s);
char *strncpy(char *dst, const char *src, size_t count);
int vsnprintf(char *dst, size_t size, const char *fmt, va_list args);
#endif
