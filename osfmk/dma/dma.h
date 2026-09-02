#pragma once

#include "types.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constants (usable from both C and C++).
// ─────────────────────────────────────────────────────────────────────────────
#define DMA_BIDIRECTIONAL  0
#define DMA_TO_DEVICE      1
#define DMA_FROM_DEVICE    2

#define DMA_ADDR_LOW_MASK  0xFFFFFFFFULL
#define DMA_ADDR_HIGH_MASK 0xFFFFFFFF00000000ULL

#define DMA_SG_MAX_ENTRIES 256

// ─────────────────────────────────────────────────────────────────────────────
// C-visible structs and API (outside any namespace).
// ─────────────────────────────────────────────────────────────────────────────

struct dma_buffer {
    void   *virt;
    u64     phys;
    usize   size;
};

struct dma_sg_entry {
    u64  phys;
    u32  length;
};

struct dma_sg_list {
    struct dma_sg_entry entries[DMA_SG_MAX_ENTRIES];
    u32                 count;
};

// ─────────────────────────────────────────────────────────────────────────────
// C-linkage API.
// ─────────────────────────────────────────────────────────────────────────────

#ifdef __cplusplus
extern "C" {
#endif

void dma_init(void);
int  dma_ready(void);
struct dma_buffer dma_alloc(usize size, usize align);
void dma_free(struct dma_buffer *buf);
u64  dma_virt_to_phys(const void *virt);
void *dma_phys_to_virt(u64 phys);
void dma_sync_for_device(const struct dma_buffer *buf);
void dma_sync_for_cpu(const struct dma_buffer *buf);
void dma_sync_bidirectional(const struct dma_buffer *buf);
int  dma_addr_ok(u64 phys, int width_64);
int  dma_aligned_ok(const struct dma_buffer *buf, int width_64);
void dma_sg_init(struct dma_sg_list *list);
int  dma_sg_add(struct dma_sg_list *list, u64 phys, u32 length);
int  dma_sg_add_buffer(struct dma_sg_list *list, const struct dma_buffer *buf);

void dma_isa_mask_channel(u8 channel);
void dma_isa_unmask_channel(u8 channel);
void dma_isa_program_channel(u8 channel, u64 phys, u16 length, u8 mode);
void dma_isa_clear_status(void);

#ifdef __cplusplus
} // extern "C"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// C++ convenience: typed aliases, constexpr helpers, SGBuilder, ISA wrappers.
// ─────────────────────────────────────────────────────────────────────────────
#ifdef __cplusplus
namespace dma {

using Buffer  = ::dma_buffer;
using SGEntry = ::dma_sg_entry;
using SGList  = ::dma_sg_list;

enum class Direction : u8 {
    ToDevice    = DMA_TO_DEVICE,
    FromDevice  = DMA_FROM_DEVICE,
    Bidirectional = DMA_BIDIRECTIONAL,
};

enum class Width : u8 {
    W32 = 0,
    W64 = 1,
};

inline constexpr usize align_up(usize size, usize align) {
    return (size + align - 1) & ~(align - 1);
}

class SGBuilder {
public:
    SGBuilder();
    void reset();
    bool add(u64 phys, u32 length);
    bool add_buffer(const Buffer &buf);
    const SGList &build();
    u32  count() const { return list_.count; }
    bool empty() const { return list_.count == 0; }
private:
    SGList list_;
};

namespace isa {
    inline constexpr u16 BASE_LOW   = 0x00;
    inline constexpr u16 BASE_HIGH  = 0xC0;
    inline constexpr u16 PAGE_PORT  = 0x81;
    inline constexpr u16 STATUS_REG = 0x08;
    inline constexpr u16 CMD_REG    = 0x08;
    inline constexpr u16 MASK_REG   = 0x0A;
    inline constexpr u16 MODE_REG   = 0x0B;
    inline constexpr u16 CLEAR_REG  = 0x0C;

    inline constexpr u8 MODE_VERIFY = 0x00;
    inline constexpr u8 MODE_WRITE  = 0x01;
    inline constexpr u8 MODE_READ   = 0x02;
    inline constexpr u8 MODE_AUTO   = 0x10;
    inline constexpr u8 MODE_DEMAND = 0x00;
    inline constexpr u8 MODE_SINGLE = 0x40;
    inline constexpr u8 MODE_BLOCK  = 0x80;

    inline void select_channel(u8 ch) { (void)ch; }
    inline void mask_channel(u8 ch)   { dma_isa_mask_channel(ch); }
    inline void unmask_channel(u8 ch) { dma_isa_unmask_channel(ch); }
    inline void program_channel(u8 ch, u64 phys, u16 len, u8 mode) {
        dma_isa_program_channel(ch, phys, len, mode);
    }
    inline void clear_status()        { dma_isa_clear_status(); }
} // namespace isa

} // namespace dma
#endif
