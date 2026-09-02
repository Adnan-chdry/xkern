#ifndef PMM_H
#define PMM_H

#include "types.h"
#include "e820.h"

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

void pmm_init(struct e820_entry *map, u32 count);
u64 pmm_alloc(void);
void pmm_free(u64 addr);
void pmm_reserve(u64 addr, u64 size);
u64 pmm_get_total_memory(void);
u32 pmm_get_free_pages(void);

#endif
