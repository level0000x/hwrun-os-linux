#include "kernel.h"
#include "paging.h"
#include "mm.h"
#include "string.h"
#include "arch/x86.h"

static uint32_t *kernel_page_dir;
void *get_kernel_page_dir(void) { return kernel_page_dir; }

void map_page(void *directory, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t *pd = directory;
    uint32_t pd_index = virt >> 22, pt_index = (virt >> 12) & 0x3ff;
    if (!(pd[pd_index] & PAGE_PRESENT)) {
        uint32_t *pt = mm_alloc_page();
        if (!pt) panic("map_page: out of memory");
        pd[pd_index] = (uint32_t)pt | PAGE_PRESENT | PAGE_WRITE;
        memset(pt, 0, PAGE_SIZE);
    }
    uint32_t *pt = (uint32_t *)(pd[pd_index] & ~0xfffu);
    pt[pt_index] = (phys & ~0xfffu) | flags | PAGE_PRESENT;
}

void unmap_page(void *directory, uint32_t virt) {
    uint32_t *pd = directory;
    uint32_t entry = pd[virt >> 22];
    if (! (entry & PAGE_PRESENT)) return;
    ((uint32_t *)(entry & ~0xfffu))[(virt >> 12) & 0x3ff] = 0;
    arch_invlpg(virt);
}

void switch_page_dir(void *directory) { write_cr3((uint32_t)directory); }

void *create_user_page_dir(void) {
    uint32_t *pd = mm_alloc_page();
    if (!pd) return NULL;
    memset(pd, 0, PAGE_SIZE);
    for (int i = 0; i < 256; i++) pd[i] = kernel_page_dir ? kernel_page_dir[i] : 0;
    return pd;
}

uint32_t get_phys_addr(void *directory, uint32_t virt) {
    uint32_t entry = ((uint32_t *)directory)[virt >> 22];
    if (!(entry & PAGE_PRESENT)) return 0;
    uint32_t page = ((uint32_t *)(entry & ~0xfffu))[(virt >> 12) & 0x3ff];
    return (page & PAGE_PRESENT) ? (page & ~0xfffu) | (virt & 0xfff) : 0;
}

void paging_init(void) {
    kernel_page_dir = mm_alloc_page();
    if (!kernel_page_dir) panic("paging_init: out of memory");
    for (uint32_t address = 0; address < 0x1000000; address += PAGE_SIZE)
        map_page(kernel_page_dir, address, address, PAGE_WRITE);
    switch_page_dir(kernel_page_dir);
    write_cr0(read_cr0() | 0x80000000u);
}
