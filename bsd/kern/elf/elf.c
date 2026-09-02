#include "elf.h"
#include "pmm.h"
#include "paging.h"
#include "string.h"
#include "klibc.h"

static u64 cli_save(void)
{
    u64 f;

    asm volatile ("pushfq; popq %0; cli" : "=r"(f));
    return f;
}

static void if_restore(u64 f)
{
    if (f & 0x200)
        asm volatile ("sti");
}

int elf_load_pd(u64 pd_phys, const u8 *img, u32 size,
                u64 *entry_out, u64 *lo_out, u64 *hi_out)
{
    u64 old_cr3;
    u64 eflags;
    int r;

    if (!pd_phys)
        return -1;

    eflags = cli_save();
    old_cr3 = paging_cr3();

    asm volatile ("movq %0, %%cr3" : : "r"(pd_phys) : "memory");

    r = elf_load(img, size, entry_out, lo_out, hi_out);

    asm volatile ("movq %0, %%cr3" : : "r"(old_cr3) : "memory");

    if_restore(eflags);
    return r;
}

int elf_valid(const u8 *img, u32 size)
{
    const struct elf64_ehdr *eh;

    if (!img || size < sizeof(struct elf64_ehdr))
        return 0;

    eh = (const struct elf64_ehdr *)img;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
        return 0;
    if (eh->e_ident[4] != 2 || eh->e_ident[5] != 1)
        return 0;                       /* ELFCLASS64 little-endian */
    if (eh->e_type != ELF_ET_EXEC || eh->e_machine != ELF_EM_X86_64)
        return 0;
    return 1;
}

int elf_load(const u8 *img, u32 size, u64 *entry_out, u64 *lo_out, u64 *hi_out)
{
    const struct elf64_ehdr *eh;
    const struct elf64_phdr *ph;
    u32 i;
    u64 lo = ~(u64)0;
    u64 hi = 0;

    if (!elf_valid(img, size))
        return -1;

    eh = (const struct elf64_ehdr *)img;
    if (eh->e_ehsize < sizeof(struct elf64_ehdr) ||
        eh->e_phentsize < sizeof(struct elf64_phdr) ||
        eh->e_phnum == 0)
        return -1;

    if ((u64)eh->e_phoff + (u64)eh->e_phnum * eh->e_phentsize > size)
        return -1;

    ph = (const struct elf64_phdr *)(img + eh->e_phoff);

    for (i = 0; i < eh->e_phnum; i++) {
        u64 v, end;

        if (ph[i].p_type != ELF_PT_LOAD)
            continue;
        if (ph[i].p_offset + ph[i].p_filesz > size)
            return -1;
        if (ph[i].p_memsz < ph[i].p_filesz)
            return -1;

        v = ph[i].p_vaddr & ~(PAGE_SIZE - 1);
        end = (ph[i].p_vaddr + ph[i].p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        for (; v < end; v += PAGE_SIZE) {
            if (!paging_alloc_and_map(v, PAGE_WRITE | PAGE_USER))
                return -1;
        }
    }

    for (i = 0; i < eh->e_phnum; i++) {
        u64 fsize = ph[i].p_filesz;
        u64 msize = ph[i].p_memsz;

        if (ph[i].p_type != ELF_PT_LOAD)
            continue;

        if (fsize)
            klibc.memcpy((void *)(uintptr_t)ph[i].p_vaddr,
                         img + ph[i].p_offset, fsize);
        if (msize > fsize)
            klibc.memset((void *)(uintptr_t)(ph[i].p_vaddr + fsize),
                         0, msize - fsize);

        if (ph[i].p_vaddr < lo)
            lo = ph[i].p_vaddr;
        if (ph[i].p_vaddr + msize > hi)
            hi = ph[i].p_vaddr + msize;
    }

    if (lo == ~(u64)0)
        return -1;

    if (entry_out)
        *entry_out = eh->e_entry;
    if (lo_out)
        *lo_out = lo;
    if (hi_out)
        *hi_out = hi;
    return 0;
}
