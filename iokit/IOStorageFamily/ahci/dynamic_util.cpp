/*
 * dynamic_util.cpp - see dynamic_util.hpp.
 */
#include "dynamic_util.hpp"

namespace ahci {

/* ---- DMA heap -------------------------------------------------------- *
 * Simple bump allocator.  We hold a "carve" page and hand out sub-page
 * regions from it; when a request would overflow the carve page (or needs
 * a stronger alignment) we grab a fresh pmm_alloc() page.  This keeps the
 * common case allocation-free and gives every buffer a 4 KiB-aligned
 * physical base, which already satisfies every AHCI alignment rule.
 */
static u8  *g_carve;     /* current carve position        */
static u8  *g_carve_end; /* end of current carve page      */

void dma_heap_init(void)
{
    g_carve = 0;
    g_carve_end = 0;
}

static void *carve_new_page(void)
{
    u64 page = pmm_alloc();
    if (!page)
        return 0;
    g_carve = (u8 *)(unsigned long)page;
    g_carve_end = g_carve + PAGE_SIZE;
    return g_carve;
}

void *dma_alloc_aligned(u32 size, u32 align)
{
    if (size == 0 || align == 0)
        return 0;

    /* align must be a power of two */
    if ((align & (align - 1)) != 0)
        return 0;

    if (!g_carve || ((u64)(g_carve_end - g_carve)) < (u64)size) {
        if (!carve_new_page())
            return 0;
    }

    uintptr_t base = (uintptr_t)g_carve;
    uintptr_t aligned = (base + (align - 1)) & ~((uintptr_t)(align - 1));
    if (aligned + size > (uintptr_t)g_carve_end) {
        if (!carve_new_page())
            return 0;
        base = (uintptr_t)g_carve;
        aligned = (base + (align - 1)) & ~((uintptr_t)(align - 1));
    }

    void *p = (void *)aligned;
    g_carve = (u8 *)(aligned + size);

    /* zero the region so stale DMA data never leaks through */
    volatile u8 *z = (volatile u8 *)p;
    for (u32 i = 0; i < size; i++)
        z[i] = 0;

    return p;
}

void *dma_alloc(u32 size)
{
    return dma_alloc_aligned(size, 16);
}

u64 dma_to_phys(const void *va)
{
    return (u64)(unsigned long)va;
}

/* ---- timing ---------------------------------------------------------- */

void delay_us(u32 us)
{
    /* Rough calibration-free spin.  Good enough for AHCI settle delays on
     * QEMU / real hardware where the controller is far slower than the CPU. */
    u32 acc = us * 200;
    for (u32 i = 0; i < acc; i++)
        asm volatile("" ::: "memory");
}

void delay_ms(u32 ms)
{
    for (u32 i = 0; i < ms; i++)
        delay_us(1000);
}

void mmio_wmb(void)
{
    asm volatile("sfence" ::: "memory");
}

}
