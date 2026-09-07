#ifndef HW_PAGING_H
#define HW_PAGING_H
#include "kernel.h"
#define PAGE_PRESENT 0x001
#define PAGE_WRITE 0x002
#define PAGE_USER 0x004
void paging_init(void);
void *get_kernel_page_dir(void);
void map_page(void *pd, uint32_t virt, uint32_t phys, uint32_t flags);
void unmap_page(void *pd, uint32_t virt);
void switch_page_dir(void *pd);
void *create_user_page_dir(void);
uint32_t get_phys_addr(void *pd, uint32_t virt);
#endif
