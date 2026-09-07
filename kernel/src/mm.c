#include "kernel.h"
#include "mm.h"
#include "string.h"

struct block {
	size_t size;
	int free;
	struct block *next;
};

static struct block *blocks;
static char *heap_end;

static size_t align_size(size_t size) { return (size + 7u) & ~7u; }

void mm_init(void) {
	uintptr_t start = ((uintptr_t)&end + 7u) & ~7u;
	blocks = (struct block *)start;
	blocks->size = 0x800000u - start - sizeof(*blocks);
	blocks->free = 1;
	blocks->next = NULL;
	heap_end = (char *)0x800000;
}

void *kmalloc(size_t size) {
	if (!size) return NULL;
	size = align_size(size);
	for (struct block *block = blocks; block; block = block->next) {
		if (!block->free || block->size < size) continue;
		if (block->size >= size + sizeof(*block) + 8u) {
			struct block *split = (struct block *)((char *)(block + 1) + size);
			split->size = block->size - size - sizeof(*block);
			split->free = 1;
			split->next = block->next;
			block->next = split;
			block->size = size;
		}
		block->free = 0;
		return block + 1;
	}
	return NULL;
}

void kfree(void *ptr) {
	if (!ptr) return;
	struct block *block = (struct block *)ptr - 1;
	block->free = 1;
	while (block->next && block->next->free) {
		block->size += sizeof(*block) + block->next->size;
		block->next = block->next->next;
	}
	for (struct block *head = blocks; head && head->next; head = head->next) {
		if (head->next == block && head->free) {
			head->size += sizeof(*head) + block->size;
			head->next = block->next;
			break;
		}
	}
}

void *mm_alloc_page(void) {
	uintptr_t raw = (uintptr_t)kmalloc(PAGE_SIZE * 2 - 1);
	if (!raw) return NULL;
	return (void *)((raw + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1));
}

void mm_free_page(void *page) { (void)page; }

void *kcalloc(size_t n, size_t size) {
	if (n && size > (size_t)-1 / n) return NULL;
	size_t total = n * size;
	void *ptr = kmalloc(total);
	return ptr ? memset(ptr, 0, total) : NULL;
}

void *krealloc(void *ptr, size_t size) {
	if (!ptr) return kmalloc(size);
	if (!size) { kfree(ptr); return NULL; }
	struct block *old = (struct block *)ptr - 1;
	if (old->size >= align_size(size)) return ptr;
	void *next = kmalloc(size);
	if (!next) return NULL;
	memcpy(next, ptr, old->size);
	kfree(ptr);
	return next;
}
