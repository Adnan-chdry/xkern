#pragma once
/*
 * dynamic_util.hpp - AHCI DMA + timing helpers.
 *
 * This is the "dynamic" side of the AHCI driver: a tiny page-granular DMA
 * heap (backed by pmm_alloc) used to carve out the physically-contiguous,
 * identity-mapped buffers AHCI needs (command lists, received-FIS, command
 * tables, identify buffers), plus a few polling aids.
 *
 * On this kernel the DMA pool is identity-mapped (VA == PA == bus address),
 * so dma_to_phys() is a straight cast. Keep that assumption here so the
 * bus addresses fed to the controller are never truncated.
 */
#include "types.h"

extern "C" {
#include "pmm.h"
}

namespace ahci {

/* Initialise the per-driver DMA heap.  Call once before any port is probed. */
void dma_heap_init(void);

/* Allocate `size` bytes, page aligned.  The buffer is zeroed.  Returns 0 if
 * the heap is exhausted.  Each allocation is satisfied from a freshly
 * pmm_alloc() page once the current carve page runs out, so any alignment
 * requirement up to 4 KiB is met for free. */
void *dma_alloc(u32 size);

/* Allocate `size` bytes aligned to `align` (power of two).  Used where AHCI
 * demands stricter alignment than a page (e.g. 1 KiB command list). */
void *dma_alloc_aligned(u32 size, u32 align);

/* Bus/physical address the device sees for a CPU virtual pointer. */
u64  dma_to_phys(const void *va);

/* Coarse busy-wait used while polling AHCI registers during boot.  Not a
 * real clock; just enough to let the controller settle between accesses. */
void delay_us(u32 us);
void delay_ms(u32 ms);

/* Store fence so cache/store writes are ordered before the doorbell MMIO. */
void mmio_wmb(void);

}
