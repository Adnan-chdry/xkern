#ifndef PAGING_H
#define PAGING_H

#include "types.h"

#define PAGE_PRESENT     0x01
#define PAGE_WRITE       0x02
#define PAGE_USER        0x04
#define PAGE_PWT         0x08   /* write-through / write-combining hint */
#define PAGE_PCD         0x10   /* page cache disable */
#define PAGE_NOEXEC      0x8000000000000000ULL

/*
 * Caching flavors for mapped regions.
 *  - PAGE_CACHE_WB : write-back (default, for system RAM back buffers)
 *  - PAGE_CACHE_WC : write-combining (framebuffers: bursty stores that
 *                    never pollute the CPU cache)
 */
#define PAGE_CACHE_WB      (PAGE_WRITE)
#define PAGE_CACHE_WC      (PAGE_WRITE | PAGE_PWT)

/* x86_64 page hierarchy indices */
#define PML4_INDEX(v)  (((v) >> 39) & 0x1FF)
#define PDPT_INDEX(v)  (((v) >> 30) & 0x1FF)
#define PD_INDEX(v)    (((v) >> 21) & 0x1FF)
#define PT_INDEX(v)    (((v) >> 12) & 0x1FF)

void paging_init(void);
void paging_map_page(u64 virtual_addr, u64 physical_addr, u32 flags);
void paging_map_region(u64 virtual_addr, u64 physical_addr, u64 size, u32 flags);
u64 paging_alloc_and_map(u64 virtual_addr, u32 flags);

u64 paging_cr3(void);
u64 paging_clone_pd(void);
u64 paging_vfork_clone(void);

void paging_dump_pd(void);
void paging_dump_pt(u64 pdpt_index, u64 pd_index);
void paging_dump_vaddr(u64 vaddr);
void paging_verify(void);

#endif
