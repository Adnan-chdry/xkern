#include "paging.h"
#include "pmm.h"
#include "cpu.h"
#include "klog.h"
#include "printf.h"
#include "klibc.h"

/*
 * x86_64 four-level paging.
 *
 * Layout:
 *   - PML4[0]      : inherited from the boot trampoline, identity-maps the
 *                    low 4 GiB (kernel image at 1M, VGA, framebuffer, ACPI).
 *   - PML4[511]    : recursive self-map.  Page tables are edited through
 *                    the recursive window or directly via the identity map
 *                    (every table page is allocated below 3 GiB by the PMM,
 *                    so plain physical pointers stay valid).
 *   - PML4[1..255] : free user space (per address space).
 *   - PML4[256..510]: kernel higher-half space (shared on clone).
 */

#define PAGE_PS  0x080   /* page size (2MiB leaf) */
#define PT_FLAG_MASK 0xFFFULL

extern char _kernel_start;
extern char _kernel_end;

/* Recursive-window addresses for a given index path (PML4[511] = &PML4). */
static inline u64 recva(u64 a, u64 b, u64 c, u64 d)
{
    u64 v = ((a & 0x1FF) << 39) | ((b & 0x1FF) << 30) |
            ((c & 0x1FF) << 21) | ((d & 0x1FF) << 12);

    if (v & (1ULL << 47))
        v |= 0xFFFF000000000000ULL;
    return v;
}

#define RECUR_PML4_VA      recva(511, 511, 511, 0)
#define RECUR_PDPT_VA(i)   recva(511, 511, 511, i)
#define RECUR_PD_VA(i, j)  recva(511, 511, i, j)
#define RECUR_PT_VA(i, j, k) recva(511, i, j, k)

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

u64 paging_cr3(void)
{
    u64 cr3;

    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static void paging_load_cr3(u64 phys)
{
    asm volatile ("mov %0, %%cr3" : : "r"(phys) : "memory");
}

static inline void invlpg(u64 va)
{
    asm volatile ("invlpg (%0)" : : "r"(va) : "memory");
}

/* Writable pointer to a page-table page (all tables live below 4 GiB and
 * are reachable through the identity map). */
static inline u64 *pt_ptr(u64 table_phys)
{
    return (u64 *)(table_phys & ~PT_FLAG_MASK);
}

static void zero_page_table(u64 phys)
{
    u64 *t = pt_ptr(phys);

    for (u32 i = 0; i < 512; i++)
        t[i] = 0;
}

void paging_map_page(u64 virtual_addr, u64 physical_addr, u32 flags)
{
    u64 a = PML4_INDEX(virtual_addr);
    u64 b = PDPT_INDEX(virtual_addr);
    u64 c = PD_INDEX(virtual_addr);
    u64 d = PT_INDEX(virtual_addr);
    u64 pml4_phys = paging_cr3() & ~PT_FLAG_MASK;
    u64 *pml4 = pt_ptr(pml4_phys);
    u64 *pdpt, *pd, *pt;

    if (!(pml4[a] & PAGE_PRESENT)) {
        u64 t = pmm_alloc();

        if (!t)
            return;
        klog("vm", "pml4[%llu] new table phys=%llx", a, t);
        zero_page_table(t);
        pml4[a] = t | PAGE_PRESENT | PAGE_WRITE;
    }

    pdpt = pt_ptr(pml4[a]);
    if (!(pdpt[b] & PAGE_PRESENT)) {
        u64 t = pmm_alloc();

        if (!t)
            return;
        klog("vm", "pdpt[%llu][%llu] new table phys=%llx", a, b, t);
        zero_page_table(t);
        pdpt[b] = t | PAGE_PRESENT | PAGE_WRITE;
    }

    pd = pt_ptr(pdpt[b]);
    if (!(pd[c] & PAGE_PRESENT)) {
        u64 t = pmm_alloc();

        if (!t)
            return;
        klog("vm", "pd[%llu][%llu] new table phys=%llx (va=%llx)",
             b, c, t, virtual_addr);
        zero_page_table(t);
        pd[c] = t | PAGE_PRESENT | PAGE_WRITE;
    } else if (pd[c] & PAGE_PS) {
        /* split an existing 2 MiB leaf into 4 KiB pages so we can map a
         * single page inside it (e.g. framebuffer remap with WC) */
        u64 t = pmm_alloc();
        u64 two_mb_base = pd[c] & ~((1ULL << 21) - 1);
        u64 leaf_flags = pd[c] & (PAGE_PRESENT | PAGE_WRITE | PAGE_USER |
                                  PAGE_PWT | PAGE_PCD);
        /* caller wants non-default caching (MMIO): apply it to every
         * entry, otherwise the 511 untouched neighbours stay mapped
         * cacheable - speculative fetches into neighbouring chipset
         * registers hard-freeze real silicon */
        u64 mmio = flags & (PAGE_PWT | PAGE_PCD);

        if (!t)
            return;
        klog("vm", "splitting 2m leaf at %llx for va=%llx (pt=%llx)",
             two_mb_base, virtual_addr, t);
        zero_page_table(t);
        u64 *nt = pt_ptr(t);
        for (u32 i = 0; i < 512; i++) {
            u64 e = (two_mb_base + ((u64)i << 12)) | leaf_flags;
            if (mmio)
                e = (e & ~(u64)(PAGE_PWT | PAGE_PCD))
                    | (flags & (PAGE_PWT | PAGE_PCD));
            nt[i] = e;
        }
        pd[c] = t | PAGE_PRESENT | PAGE_WRITE | (leaf_flags & ~PAGE_PRESENT);
        klog("vm", "split filled pt=%llx", t);
        invlpg(virtual_addr);
    }

    pt = pt_ptr(pd[c]);
    pt[d] = (physical_addr & ~PT_FLAG_MASK) |
            ((u64)(flags & 0xFFF) & ~(u64)PAGE_PS) | PAGE_PRESENT;

    invlpg(virtual_addr);
}

void paging_map_region(u64 virtual_addr, u64 physical_addr, u64 size, u32 flags)
{
    u64 end = virtual_addr + size;

    /* one-shot diagnostics: which table walk level is cold, and what
     * currently sits at the PD slot covering the region */
    {
        u64 a = PML4_INDEX(virtual_addr);
        u64 b = PDPT_INDEX(virtual_addr);
        u64 c = PD_INDEX(virtual_addr);
        u64 *pml4 = pt_ptr(paging_cr3());
        u64 pde_val = 0xDEAD0000ULL;
        if (pml4[a] & PAGE_PRESENT) {
            u64 *pdpt = pt_ptr(pml4[a]);
            if (pdpt[b] & PAGE_PRESENT)
                pde_val = pt_ptr(pdpt[b])[c];
        }
      /*disable logging for now*/
  //      klog("vm", "map_region va=%llx sz=%llx idx=%llu/%llu/%llu "
      //       "pde=%llx", virtual_addr, size, a, b, c, pde_val);
    }

    for (u64 v = virtual_addr, p = physical_addr; v < end; v += PAGE_SIZE, p += PAGE_SIZE)
        paging_map_page(v, p, flags);

   // klog("vm", "map_region va=%llx done", virtual_addr);
}

u64 paging_alloc_and_map(u64 virtual_addr, u32 flags)
{
    u64 phys = pmm_alloc();

    if (!phys)
        return 0;
    paging_map_page(virtual_addr, phys, flags);
    return phys;
}

/*
 * Fresh address space for a new task: shares only the boot identity tree
 * (PML4[0], where the kernel and user images live) and the kernel half.
 * User space above the low 4 GiB starts empty.
 */
u64 paging_clone_pd(void)
{
    u64 new_pml4;
    u64 *src;
    u64 *dst;
    u64 eflags = cli_save();

    new_pml4 = pmm_alloc();
    if (!new_pml4) {
        if_restore(eflags);
        return 0;
    }

    /* the new table is not mapped anywhere yet: fill it through its
     * identity-mapped physical address (always < 4 GiB) */
    dst = pt_ptr(new_pml4);
    src = pt_ptr(paging_cr3());

    zero_page_table(new_pml4);
    for (u32 i = 256; i < 511; i++)     /* kernel half, shared */
        dst[i] = src[i];
    dst[0] = src[0];                    /* low-4G identity tree, shared */
    dst[511] = new_pml4 | PAGE_PRESENT | PAGE_WRITE;

    if_restore(eflags);
    return new_pml4;
}

/*
 * Full address-space clone for vfork: every PML4 entry is copied, sharing
 * whole subtrees with the parent until the child execs or exits.  Only
 * the self-reference becomes private.
 */
u64 paging_vfork_clone(void)
{
    u64 parent_cr3 = paging_cr3();
    u64 new_pml4;
    u64 *src;
    u64 *dst;
    u64 eflags = cli_save();

    new_pml4 = pmm_alloc();
    if (!new_pml4) {
        if_restore(eflags);
        return 0;
    }

    src = pt_ptr(parent_cr3);
    dst = pt_ptr(new_pml4);

    for (u32 i = 0; i < 511; i++)
        dst[i] = src[i];
    dst[511] = new_pml4 | PAGE_PRESENT | PAGE_WRITE;

    if_restore(eflags);
    return new_pml4;
}

void paging_dump_pd(void)
{
    u64 *pml4 = pt_ptr(paging_cr3());

    klibc.printf("\n:: PML4 ::\n");
    for (u64 i = 0; i < 512; i++)
    {
        if (pml4[i] & PAGE_PRESENT)
        {
            klibc.printf("  [%llx] next=%llx flags=%llx  va range: %llx - %llx\n",
                   i, pml4[i] & ~PT_FLAG_MASK, pml4[i] & PT_FLAG_MASK,
                   i << 39, ((i + 1) << 39) - 1);
        }
    }
    klibc.printf(":: End PML4 ::\n");
}

void paging_dump_pt(u64 pdpt_index, u64 pd_index)
{
    u64 *pml4 = pt_ptr(paging_cr3());
    u64 *pdpt, *pd, *pt;

    if (!(pml4[pdpt_index] & PAGE_PRESENT))
    {
        klibc.printf("PDPT not present for PML4 index %llx\n", pdpt_index);
        return;
    }

    pdpt = pt_ptr(pml4[pdpt_index]);
    if (!(pdpt[pd_index] & PAGE_PRESENT))
    {
        klibc.printf("PD not present for PDPT index %llx\n", pd_index);
        return;
    }

    pd = pt_ptr(pdpt[pd_index]);

    klibc.printf("\nPage table [PDPT %llx][PD %llx] covers %llx - %llx\n",
           pdpt_index, pd_index,
           (pdpt_index << 30) | (pd_index << 21),
           ((pdpt_index << 30) | ((pd_index + 1) << 21)) - 1);

    if (pd[pd_index] & PAGE_PS)
    {
        klibc.printf("  2MiB leaf: base=%llx flags=%llx\n",
               pd[pd_index] & ~PT_FLAG_MASK, pd[pd_index] & PT_FLAG_MASK);
        klibc.printf("\n");
        return;
    }

    pt = pt_ptr(pd[pd_index]);
    for (u64 i = 0; i < 512; i++)
    {
        if (pt[i] & PAGE_PRESENT)
        {
            klibc.printf("  vaddr=%llx phys=%llx flags=%llx\n",
                   (pdpt_index << 30) | (pd_index << 21) | (i << 12),
                   pt[i] & ~PT_FLAG_MASK, pt[i] & PT_FLAG_MASK);
        }
    }
    klibc.printf("\n");
}

void paging_dump_vaddr(u64 vaddr)
{
    u64 *pml4 = pt_ptr(paging_cr3());
    u64 a = PML4_INDEX(vaddr), b = PDPT_INDEX(vaddr);
    u64 c = PD_INDEX(vaddr), d = PT_INDEX(vaddr);
    u64 pml4e = pml4[a];

    klibc.printf("\nAddress %llx\n", vaddr);
    klibc.printf("  PML4=%llx PDPT=%llx PD=%llx PT=%llx\n", a, b, c, d);
    klibc.printf("  PML4E: %llx\n", pml4e);
    klibc.printf("    present=%c write=%c user=%c\n",
           (pml4e & PAGE_PRESENT) ? '1' : '0',
           (pml4e & PAGE_WRITE) ? '1' : '0',
           (pml4e & PAGE_USER) ? '1' : '0');

    if (!(pml4e & PAGE_PRESENT))
    {
        klibc.printf("\n");
        return;
    }

    u64 *pdpt = pt_ptr(pml4e);
    u64 pdpte = pdpt[b];

    klibc.printf("  PDPTE: %llx\n", pdpte);
    if (!(pdpte & PAGE_PRESENT))
    {
        klibc.printf("\n");
        return;
    }

    u64 *pd = pt_ptr(pdpte);
    u64 pde = pd[c];

    klibc.printf("  PDE: %llx%s\n", pde, (pde & PAGE_PS) ? " (2MiB)" : "");
    if (!(pde & PAGE_PRESENT))
    {
        klibc.printf("\n");
        return;
    }

    if (pde & PAGE_PS)
    {
        klibc.printf("    phys page=%llx (2MiB)\n",
               pde & ~((1ULL << 21) - 1));
    }
    else
    {
        u64 *pt = pt_ptr(pde);
        u64 pte = pt[d];

        klibc.printf("  PTE: %llx\n", pte);
        klibc.printf("    present=%c write=%c\n",
               (pte & PAGE_PRESENT) ? '1' : '0',
               (pte & PAGE_WRITE) ? '1' : '0');
        if (pte & PAGE_PRESENT)
            klibc.printf("    phys page=%llx\n", pte & ~PT_FLAG_MASK);
    }
    klibc.printf("\n");
}

void paging_verify(void)
{
    u64 cr3_val = paging_cr3();
    u64 cr0_val;

    asm volatile ("mov %%cr0, %0" : "=r"(cr0_val));

    klibc.printf("\n=== Paging Verify ===\n");
    klibc.printf("CR0=%llx\nCR3=%llx\n", cr0_val, cr3_val);

    u64 pg = (cr0_val >> 31) & 1;
    klibc.printf("Paging enabled: %c\n", pg ? 'Y' : 'N');
    u64 lm = 0;
    u32 a, b, c, d;

    cpuid(0x80000001, &a, &b, &c, &d);
    lm = (d >> 29) & 1;
    klibc.printf("Long mode capable: %c\n", lm ? 'Y' : 'N');

    paging_dump_vaddr(0x100000);
    paging_dump_vaddr(0xB8000);

    klibc.printf("Recursive mapping (PML4[511]): %llx\n",
                 pt_ptr(cr3_val)[511]);
    u64 rec_phys = pt_ptr(cr3_val)[511] & ~PT_FLAG_MASK;
    klibc.printf("Self-map target=%llx (should equal CR3=%llx)\n",
           rec_phys, cr3_val);

    klibc.printf(rec_phys == (cr3_val & ~PT_FLAG_MASK)
           ? "Recursive mapping: OK\n" : "Recursive mapping: MISMATCH\n");

    klibc.printf("end of verification\n");
}

void paging_init(void)
{
    u64 new_pml4 = pmm_alloc();
    u64 old_cr3 = paging_cr3();
    u64 *src, *dst;

    /*
     * The boot trampoline already runs us in long mode with an identity
     * map of the low 4 GiB in CR3.  Build our own PML4 that inherits that
     * tree (entry 0) plus the kernel-half convention, add the recursive
     * self-map, and swap it in.
     */
    zero_page_table(new_pml4);

    src = pt_ptr(old_cr3);
    dst = pt_ptr(new_pml4);

    dst[0] = src[0];                        /* inherit identity low 4 GiB */
    for (u32 i = 256; i < 511; i++)         /* kernel half (none yet) */
        dst[i] = src[i];
    dst[511] = new_pml4 | PAGE_PRESENT | PAGE_WRITE;

    paging_load_cr3(new_pml4);

    klog("vm", "paging_init(): long-mode 4-level tables active");
}
