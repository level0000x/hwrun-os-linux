#include "kernel.h"
#include "elf.h"
#include "loader.h"
#include "string.h"

int elf_load(void *data, void **entry, void **base, size_t *size) {
    Elf32_Ehdr *header = data;
    if (!data || header->e_ident[0] != 0x7f || header->e_ident[1] != 'E' || header->e_ident[2] != 'L' || header->e_ident[3] != 'F') return -1;
    if (header->e_type != ET_EXEC || header->e_ehsize != sizeof(Elf32_Ehdr) || header->e_phentsize != sizeof(Elf32_Phdr) || !header->e_phnum) return -2;
    if (header->e_phoff > 0x100000u || header->e_phnum > 128u || header->e_phoff + (uint32_t)header->e_phnum * sizeof(Elf32_Phdr) < header->e_phoff) return -2;
    Elf32_Phdr *program = (Elf32_Phdr *)((char *)data + header->e_phoff);
    uint32_t low = 0xffffffffu, high = 0;
    for (uint16_t i = 0; i < header->e_phnum; i++) if (program[i].p_type == PT_LOAD) {
        uint32_t segment_end = program[i].p_vaddr + program[i].p_memsz;
        if (program[i].p_filesz > program[i].p_memsz || segment_end < program[i].p_vaddr || program[i].p_vaddr < PLUGIN_BASE_ADDR || segment_end >= 0x80000000u) return -3;
        if (program[i].p_vaddr < low) low = program[i].p_vaddr;
        if (segment_end > high) high = segment_end;
    }
    if (low == 0xffffffffu || high <= low || header->e_entry < low || header->e_entry >= high) return -4;
    for (uint16_t i = 0; i < header->e_phnum; i++) if (program[i].p_type == PT_LOAD) { memcpy((void *)program[i].p_vaddr, (char *)data + program[i].p_offset, program[i].p_filesz); if (program[i].p_memsz > program[i].p_filesz) memset((char *)program[i].p_vaddr + program[i].p_filesz, 0, program[i].p_memsz - program[i].p_filesz); }
    *entry = (void *)header->e_entry; *base = (void *)low; *size = high - low; return 0;
}
