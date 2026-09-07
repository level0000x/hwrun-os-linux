#ifndef HW_ELF_H
#define HW_ELF_H
#include <stdint.h>
typedef uint16_t Elf32_Half; typedef uint32_t Elf32_Word; typedef int32_t Elf32_Sword; typedef uint32_t Elf32_Addr; typedef uint32_t Elf32_Off;
#define EI_NIDENT 16
struct elf32_ehdr { unsigned char e_ident[EI_NIDENT]; Elf32_Half e_type,e_machine; Elf32_Word e_version; Elf32_Addr e_entry; Elf32_Off e_phoff,e_shoff; Elf32_Word e_flags; Elf32_Half e_ehsize,e_phentsize,e_phnum,e_shentsize,e_shnum,e_shstrndx; };
typedef struct elf32_ehdr Elf32_Ehdr;
struct elf32_phdr { Elf32_Word p_type; Elf32_Off p_offset; Elf32_Addr p_vaddr,p_paddr; Elf32_Word p_filesz,p_memsz,p_flags,p_align; };
typedef struct elf32_phdr Elf32_Phdr;
#define PT_LOAD 1
#define ET_EXEC 2
#endif
