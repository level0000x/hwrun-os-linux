#include "kernel.h"
#include "loader.h"
#include "elf.h"

extern int elf_load(void *data, void **entry, void **base, size_t *size);
int loader_init(void) { return 0; }
void *loader_load(const char *path, struct task **out_task) {
    (void)path; (void)out_task;
    void *image = (void *)PLUGIN_BASE_ADDR;
    if (*(uint32_t *)image != 0x464c457f) { printk("loader: BUS image unavailable\n"); return NULL; }
    void *entry, *base; size_t size;
    if (elf_load(image, &entry, &base, &size) != 0) { printk("loader: invalid ELF image\n"); return NULL; }
    printk("loader: entry=%x size=%u\n", (uint32_t)entry, (uint32_t)size);
    return entry;
}
