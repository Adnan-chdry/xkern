#include "dma.h"

extern "C" {
#include "pmm.h"
#include "paging.h"
#include "io.h"
#include "klog.h"
}

#include <cstddef>

#define MOD kerndma;

// ─────────────────────────────────────────────────────────────────────────────
// Internal C++ implementation (only used within this translation unit).
// ─────────────────────────────────────────────────────────────────────────────
namespace {

static bool g_dma_ready = false;

static void cache_flush_range(u64 base, usize size)
{
    for (usize off = 0; off < size; off += 64)
        asm volatile ("clflush (%0)" : : "r"(base + off) : "memory");
    asm volatile ("mfence" ::: "memory");
}

static u64 internal_virt_to_phys(const void *virt)
{
    u64 va = reinterpret_cast<u64>(virt);

    // Identity-mapped region: phys == virt for addresses below 4 GiB.
    if (va < 0x100000000ULL)
        return va;

    // Walk the page tables via the recursive self-map at PML4[511].
    u64 a = PML4_INDEX(va);
    u64 b = PDPT_INDEX(va);
    u64 c = PD_INDEX(va);
    u64 d = PT_INDEX(va);

    u64 *rec_pml4 = reinterpret_cast<u64 *>(
        (511ULL << 39) | (511ULL << 30) | (511ULL << 21) | (511ULL << 12));

    u64 pml4e = rec_pml4[511];
    if (!(pml4e & PAGE_PRESENT))
        return 0;

    u64 *pdpt = reinterpret_cast<u64 *>(pml4e & ~0xFFFULL);
    u64 pdpte = pdpt[a];
    if (!(pdpte & PAGE_PRESENT))
        return 0;

    u64 *pd = reinterpret_cast<u64 *>(pdpte & ~0xFFFULL);
    u64 pde = pd[b];
    if (!(pde & PAGE_PRESENT))
        return 0;

    if (pde & 0x080)
        return (pde & ~((1ULL << 21) - 1)) | (va & ((1ULL << 21) - 1));

    u64 *pt = reinterpret_cast<u64 *>(pde & ~0xFFFULL);
    u64 pte = pt[d];
    (void)c;
    if (!(pte & PAGE_PRESENT))
        return 0;

    return (pte & ~0xFFFULL) | (va & 0xFFFULL);
}

static void internal_isa_mask(u8 channel)
{
    outb(0x0A, 0x04 | (channel & 0x03));
}

static void internal_isa_unmask(u8 channel)
{
    outb(0x0A, channel & 0x03);
}

static void internal_isa_clear()
{
    outb(0x0C, 0xFF);
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// C-linkage API — called from kernel_main and from C drivers.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" {

void dma_init(void)
{
    // Mask all ISA DMA channels (0-3) to prevent stale transfers.
    internal_isa_mask(0);
    internal_isa_mask(1);
    internal_isa_mask(2);
    internal_isa_mask(3);
    internal_isa_clear();

    g_dma_ready = true;
    klog("libdma", "DMA subsystem initialised");
}

int dma_ready(void)
{
    return g_dma_ready ? 1 : 0;
}

struct dma_buffer dma_alloc(usize size, usize align)
{
    struct dma_buffer buf = {nullptr, 0, 0};

    if (size == 0)
        return buf;

    usize pages = dma::align_up(size, PAGE_SIZE) / PAGE_SIZE;
    usize alloc_size = pages * PAGE_SIZE;

    if (align > PAGE_SIZE) {
        for (u32 attempt = 0; attempt < 8; ++attempt) {
            u64 first_phys = pmm_alloc();
            if (!first_phys)
                return buf;

            if ((first_phys & (align - 1)) == 0) {
                buf.phys = first_phys;
                buf.virt = reinterpret_cast<void *>(first_phys);
                buf.size = alloc_size;
                for (usize i = 0; i < alloc_size; i += sizeof(u64))
                    *reinterpret_cast<u64 *>(
                        static_cast<u8 *>(buf.virt) + i) = 0;
                return buf;
            }
            pmm_free(first_phys);
        }
    }

    u64 phys = pmm_alloc();
    if (!phys)
        return buf;

    buf.phys = phys;
    buf.virt = reinterpret_cast<void *>(phys);
    buf.size = alloc_size;

    for (usize i = 0; i < alloc_size; i += sizeof(u64))
        *reinterpret_cast<u64 *>(
            static_cast<u8 *>(buf.virt) + i) = 0;

    return buf;
}

void dma_free(struct dma_buffer *buf)
{
    if (!buf || !buf->virt)
        return;

    usize pages = buf->size / PAGE_SIZE;
    u64 base = buf->phys & ~(PAGE_SIZE - 1);

    for (usize i = 0; i < pages; ++i)
        pmm_free(base + i * PAGE_SIZE);

    buf->virt = nullptr;
    buf->phys = 0;
    buf->size = 0;
}

u64 dma_virt_to_phys(const void *virt)
{
    return internal_virt_to_phys(virt);
}

void *dma_phys_to_virt(u64 phys)
{
    return reinterpret_cast<void *>(phys);
}

void dma_sync_for_device(const struct dma_buffer *buf)
{
    if (!buf || !buf->virt)
        return;
    cache_flush_range(buf->phys, buf->size);
}

void dma_sync_for_cpu(const struct dma_buffer *buf)
{
    if (!buf || !buf->virt)
        return;
    cache_flush_range(buf->phys, buf->size);
}

void dma_sync_bidirectional(const struct dma_buffer *buf)
{
    if (!buf || !buf->virt)
        return;
    cache_flush_range(buf->phys, buf->size);
}

int dma_addr_ok(u64 phys, int width_64)
{
    if (!width_64)
        return phys <= 0xFFFFFFFFULL;
    return 1;
}

int dma_aligned_ok(const struct dma_buffer *buf, int width_64)
{
    if (!buf)
        return 0;
    usize required = width_64 ? 8 : 4;
    return (buf->phys & (required - 1)) == 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Scatter-gather helpers (C API).
// ─────────────────────────────────────────────────────────────────────────────

void dma_sg_init(struct dma_sg_list *list)
{
    if (!list)
        return;
    list->count = 0;
    for (u32 i = 0; i < DMA_SG_MAX_ENTRIES; ++i) {
        list->entries[i].phys = 0;
        list->entries[i].length = 0;
    }
}

int dma_sg_add(struct dma_sg_list *list, u64 phys, u32 length)
{
    if (!list || list->count >= DMA_SG_MAX_ENTRIES || length == 0)
        return 0;
    list->entries[list->count].phys = phys;
    list->entries[list->count].length = length;
    ++list->count;
    return 1;
}

int dma_sg_add_buffer(struct dma_sg_list *list, const struct dma_buffer *buf)
{
    if (!list || !buf || !buf->virt || buf->size == 0)
        return 0;

    usize remaining = buf->size;
    u64   phys = buf->phys;

    while (remaining > 0) {
        u32 chunk = (remaining > 0xFFFFFFFFULL)
                        ? 0xFFFFFFFFU
                        : static_cast<u32>(remaining);

        if (!dma_sg_add(list, phys, chunk))
            return 0;

        phys      += chunk;
        remaining -= chunk;
    }
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// ISA 8237 DMA controller helpers (C API).
// ─────────────────────────────────────────────────────────────────────────────

void dma_isa_mask_channel(u8 channel)
{
    internal_isa_mask(channel);
}

void dma_isa_unmask_channel(u8 channel)
{
    internal_isa_unmask(channel);
}

void dma_isa_clear_status(void)
{
    internal_isa_clear();
}

void dma_isa_program_channel(u8 channel, u64 phys, u16 length, u8 mode)
{
    u8 ch = channel & 0x03;
    u16 base = (channel < 4) ? 0x00 : 0xC0;
    u16 off  = (channel < 4) ? static_cast<u16>(ch * 2) : static_cast<u16>(ch * 8);

    internal_isa_mask(channel);
    internal_isa_clear();

    outb(0x0B, (ch & 0x03) | mode);                    // mode register

    outb(base + off,     static_cast<u8>(phys & 0xFF));
    outb(base + off,     static_cast<u8>((phys >> 8) & 0xFF));
    outb(0x81 + ch,      static_cast<u8>((phys >> 16) & 0xFF));

    outb(base + off + 1, static_cast<u8>(length & 0xFF));
    outb(base + off + 1, static_cast<u8>((length >> 8) & 0xFF));

    internal_isa_unmask(channel);
}

} // extern "C"

// ─────────────────────────────────────────────────────────────────────────────
// C++ only: SGBuilder implementation.
// ─────────────────────────────────────────────────────────────────────────────
#ifdef __cplusplus
namespace dma {

SGBuilder::SGBuilder()
{
    reset();
}

void SGBuilder::reset()
{
    list_.count = 0;
    for (u32 i = 0; i < DMA_SG_MAX_ENTRIES; ++i) {
        list_.entries[i].phys = 0;
        list_.entries[i].length = 0;
    }
}

bool SGBuilder::add(u64 phys, u32 length)
{
    if (list_.count >= DMA_SG_MAX_ENTRIES || length == 0)
        return false;
    list_.entries[list_.count].phys = phys;
    list_.entries[list_.count].length = length;
    ++list_.count;
    return true;
}

bool SGBuilder::add_buffer(const Buffer &buf)
{
    if (!buf.virt || buf.size == 0)
        return false;

    usize remaining = buf.size;
    u64   phys = buf.phys;

    while (remaining > 0) {
        u32 chunk = (remaining > 0xFFFFFFFFULL)
                        ? 0xFFFFFFFFU
                        : static_cast<u32>(remaining);
        if (!add(phys, chunk))
            return false;
        phys      += chunk;
        remaining -= chunk;
    }
    return true;
}

const SGList &SGBuilder::build()
{
    return list_;
}

} // namespace dma
#endif
