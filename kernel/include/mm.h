#ifndef HW_MM_H
#define HW_MM_H
#include "kernel.h"
void *mm_alloc_page(void);
void mm_free_page(void *page);
#endif
