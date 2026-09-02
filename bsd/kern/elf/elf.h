#ifndef ELF_H
#define ELF_H

#include "types.h"

#define ELF_PT_LOAD 1
#define ELF_ET_EXEC 2
#define ELF_EM_386      3
#define ELF_EM_X86_64   62

/* 64-bit ELF headers (xkern userland is ELF64 x86_64) */
struct elf64_ehdr {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} __attribute__((packed));

int elf_valid(const u8 *img, u32 size);
int elf_load(const u8 *img, u32 size, u64 *entry_out, u64 *lo_out, u64 *hi_out);
int elf_load_pd(u64 pd_phys, const u8 *img, u32 size, u64 *entry_out,
                u64 *lo_out, u64 *hi_out);

#endif
