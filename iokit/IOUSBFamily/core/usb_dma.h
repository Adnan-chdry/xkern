/*
 * usb_dma.h - proper DMA abstraction for the USB stack.
 *
 * The old code (usb_dma_flush/usb_dma_invalidate) had three real
 * problems on this x86_64 kernel:
 *
 *   1. It flushed with a hard-coded 32-byte stride.  x86 cache lines are
 *      64 bytes on every modern core, so only every other line was
 *      touched (it still happened to cover, but redundantly and only by
 *      luck).  We now query the real line size via CPUID.
 *
 *   2. It used a single "barrier()" that is only a compiler fence
 *      (asm volatile("":::"memory")).  That does NOT order the clflush
 *      writes against the later device doorbell MMIO write, so the
 *      controller could read stale data.  We issue sfence after cache
 *      maintenance and before ringing the doorbell.
 *
 *   3. It handed the device the *CPU virtual* address cast to u32.  On a
 *      64-bit kernel that buffer lives wherever the linker put .bss, which
 *      may be above 4 GiB, and the truncation loses the high bits.  The
 *      pool is now backed by pmm_alloc() frames (guaranteed < 3 GiB) and
 *      every bus address is a full 64-bit dma_addr_t.
 *
 * Direction matters:
 *   TO_DEVICE     CPU wrote the buffer, device will read it  -> write-back
 *   FROM_DEVICE   device wrote the buffer, CPU will read it   -> invalidate
 *   BIDIRECTIONAL both                                              -> both
 *
 * An optional IOMMU/bounce layer (USB_DMA_BOUNCE) lets a buffer that is
 * not itself DMA-able (above 4 GiB, or cache-aliased) be copied through a
 * dedicated low-memory bounce page whose bus address is given to the
 * device.  The default identity mapping simply returns the buffer's own
 * bus address.
 */
#ifndef USB_DMA_H
#define USB_DMA_H

#include <stddef.h>
#include "types.h"

/*
 * Bus/physical address the device sees.  Always 64-bit: even though this
 * kernel only feeds controllers memory below 4 GiB today, treating it as
 * u64 keeps the contract honest and stops silent truncation.
 */
typedef u64 dma_addr_t;

#define USB_DMA_TO_DEVICE      1
#define USB_DMA_FROM_DEVICE    2
#define USB_DMA_BIDIRECTIONAL  3

/* ------------------------------------------------------------------ *
 * Cache maintenance (x86).  Addresses must be virtually mapped; on this
 * kernel the DMA pool is identity-mapped so VA == PA.
 * ------------------------------------------------------------------ */

/* Clean (write-back) the CPU cache so the device can read latest data. */
void usb_dma_cache_clean(const void *va, u32 len);

/* Invalidate the CPU cache so the CPU re-reads what the device wrote. */
void usb_dma_cache_inv(void *va, u32 len);

/* Direction-aware combined op, issued before handing the buffer over. */
void usb_dma_cache_sync(void *va, u32 len, int dir);

/* Store/serialization fence: order cache maintenance + prior stores
 * before the device doorbell MMIO write that follows. */
void usb_dma_wmb(void);

/* ------------------------------------------------------------------ *
 * Mapping API.  Drivers that build their own rings/queues can keep
 * using usb_dma_alloc() and usb_dma_to_bus(); drivers that map an
 * arbitrary caller buffer should use the map/unmap pair.
 * ------------------------------------------------------------------ */

struct usb_dma_map {
    dma_addr_t dma;        /* bus address handed to the device */
    void      *va;         /* CPU virtual address of the buffer */
    u32        len;
    int        dir;
    /* bounce bookkeeping - only used when USB_DMA_BOUNCE is on */
    dma_addr_t bounce_dma;
    void      *bounce_va;
    int        bounced;
};

/*
 * Map a CPU buffer for DMA.  Fills *m and returns the bus address.
 * For TO_DEVICE the buffer is cleaned; for FROM_DEVICE it is left alone
 * (cleaned on unmap/sync_for_cpu after the device wrote it).
 */
dma_addr_t usb_dma_map_single(void *va, u32 len, int dir,
                              struct usb_dma_map *m);

/* Done with the buffer.  For FROM_DEVICE the CPU copy is invalidated so
 * the driver sees the device's data.  Bounce data is copied back here. */
void usb_dma_unmap_single(struct usb_dma_map *m);

/* Re-sync for the CPU to read after the device touched the buffer. */
void usb_dma_sync_for_cpu(struct usb_dma_map *m);

/* Re-sync for the device to (re)read after the CPU touched the buffer. */
void usb_dma_sync_for_device(struct usb_dma_map *m);

/* Identity bus address of a DMA-pool virtual pointer.  With the default
 * (non-IOMMU) pool VA == PA == bus, but go through this helper so a real
 * IOMMU can be dropped in later. */
dma_addr_t usb_dma_to_bus(const void *va);
void     *usb_dma_to_va(dma_addr_t dma);

/* ------------------------------------------------------------------ *
 * Pool allocator.  Memory comes from pmm_alloc() (always < 3 GiB) and is
 * identity-mapped, so it is unconditionally DMA-able by 32-bit controllers.
 * ------------------------------------------------------------------ */
void usb_dma_pool_init(void);
void usb_dma_reset(void);
void *usb_dma_alloc(size_t size);
void *usb_dma_alloc_aligned(size_t size, u32 align);

/* Compatibility shims - prefer the directional API above. */
static inline void usb_dma_flush(const void *ptr, u32 len)
{
    usb_dma_cache_clean(ptr, len);
}
static inline void usb_dma_invalidate(void *ptr, u32 len)
{
    usb_dma_cache_inv(ptr, len);
}

#endif /* USB_DMA_H */
