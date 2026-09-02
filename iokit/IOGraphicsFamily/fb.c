#include "fb.h"
#include "font.h"
#include "buffer.h"
#include "paging.h"
#include "pmm.h"
#include "string.h"
#include "klibc.h"

static struct framebuffer fb;
static int fb_ready = 0;
static uint32_t fb_ring_top;

static uint32_t fb_dirty_min;
static uint32_t fb_dirty_max;

/* derived pixel format (computed once in framebuffer_init) */
static uint8_t  fb_bytes_pp;            /* bpp / 8                          */
static uint8_t  fb_rloss, fb_gloss, fb_bloss;   /* bits dropped: 8 - size   */

/*
 * Canonical colour format used by every caller (fonts, LVGL, games) is
 * 0x00RRGGBB.  The hardware layout is whatever GRUB reported via the
 * Multiboot 2 framebuffer tag (masks + shifts), at whatever bit depth
 * the video mode uses.  fb_pack() converts canonical -> hardware.
 */
static inline uint32_t fb_pack(uint32_t color)
{
    uint32_t r = (color >> 16) & 0xFF;
    uint32_t g = (color >> 8) & 0xFF;
    uint32_t b = color & 0xFF;

    return ((r >> fb_rloss) << fb.red_mask_shift) |
           ((g >> fb_gloss) << fb.green_mask_shift) |
           ((b >> fb_bloss) << fb.blue_mask_shift);
}

/*
 * Map the hardware framebuffer for writing.
 *
 * The region is mapped write-combining (PAGE_CACHE_WC): the CPU's write
 * combining buffers coalesce the bursty row copies in framebuffer_flush()
 * and, unlike write-back, the stores never evict hot lines from the cache.
 * On x86_64 the low-4G identity map covers QEMU-style framebuffers
 * directly; anything above 4 GiB is parked in the FB_VMAP_BASE window.
 */
#define FB_VMAP_BASE  0xFB000000ull

static void fb_mark_dirty(uint32_t y)
{
    if (y < fb_dirty_min) fb_dirty_min = y;
    if (y + 1 > fb_dirty_max) fb_dirty_max = y + 1;
}

static void fb_reset_dirty(void)
{
    fb_dirty_min = fb.height;
    fb_dirty_max = 0;
}

static void fb_mark_all_dirty(void)
{
    fb_dirty_min = 0;
    fb_dirty_max = fb.height;
}

static uint8_t *fb_row_ptr(uint32_t y)
{
    uint8_t *base = framebuffer_buffer_ready() ? framebuffer_buffer_back() : fb.address;
    return base + (fb_ring_top + y) * fb.pitch;
}

static void fb_map_hw(void)
{
    uint64_t phys = (uint64_t)(uintptr_t)fb.address;
    uint64_t size = (uint64_t)fb.pitch * fb.height;

    if (phys + size > 0x100000000ULL) {
        for (uint64_t i = 0; i < size; i += PAGE_SIZE)
            paging_map_page(FB_VMAP_BASE + i, phys + i, PAGE_CACHE_WC);
        fb.address = (uint8_t *)(uintptr_t)FB_VMAP_BASE;
    } else {
        paging_map_region(phys, phys, size, PAGE_CACHE_WC);
    }
}

/* qword burst copy: fastest way to push a whole row into a WC region */
static void fb_copy_fast(void *dst, const void *src, uint32_t bytes)
{
    uint64_t n = bytes >> 3;
    uint32_t rem = bytes & 7;

    asm volatile ("cld; rep movsq"
                  : "+S"(src), "+D"(dst),
                    "+c"(n)
                  :
                  : "memory");

    if (rem) {
        uint8_t *d = (uint8_t *)dst;
        const uint8_t *s = (const uint8_t *)src;
        while (rem--)
            *d++ = *s++;
    }
}

/*
 * Fill `n` hardware-format pixels starting at `p` (used by the fill paths).
 */
static inline void fb_store_pixel(uint8_t *p, uint32_t v)
{
    switch (fb_bytes_pp) {
    case 4:
        *(uint32_t *)p = v;
        break;
    case 3:
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)(v >> 8);
        p[2] = (uint8_t)(v >> 16);
        break;
    case 2:
        *(uint16_t *)p = (uint16_t)v;
        break;
    default:
        *p = (uint8_t)v;
        break;
    }
}

void framebuffer_init(
    uint64_t address,
    uint32_t width,
    uint32_t height,
    uint32_t pitch,
    uint8_t bpp,

    uint8_t red_mask_size,
    uint8_t red_mask_shift,

    uint8_t green_mask_size,
    uint8_t green_mask_shift,

    uint8_t blue_mask_size,
    uint8_t blue_mask_shift
)
{
    fb.address = (uint8_t *)(uintptr_t)address;

    fb.width  = width;
    fb.height = height;
    fb.pitch  = pitch;
    fb.bpp    = bpp;

    fb.red_mask_size  = red_mask_size;
    fb.red_mask_shift = red_mask_shift;

    fb.green_mask_size  = green_mask_size;
    fb.green_mask_shift = green_mask_shift;

    fb.blue_mask_size  = blue_mask_size;
    fb.blue_mask_shift = blue_mask_shift;

    /* ---- derive the pixel format -------------------------------- */
    if (bpp < 8 || bpp % 8 || !width || !height || !pitch) {
        klog("kernel.HWframe", "unsupported framebuffer mode "
             "%ux%u %u bpp, keeping text console",
             width, height, bpp);
        fb_ready = 0;
        return;
    }
    fb_bytes_pp = bpp / 8;

    /*
     * Standard RGB layouts per depth, used when the bootloader omits
     * channel masks or reports a channel with zero width (a mode where
     * a channel can't be represented would render wrong colours).
     */
    if (!red_mask_size || !green_mask_size || !blue_mask_size) {
        switch (bpp) {
        case 32:
        case 24:
            red_mask_size = 8;  red_mask_shift = 16;
            green_mask_size = 8; green_mask_shift = 8;
            blue_mask_size = 8;  blue_mask_shift = 0;
            break;
        case 16:
            red_mask_size = 5;  red_mask_shift = 11;
            green_mask_size = 6; green_mask_shift = 5;
            blue_mask_size = 5;  blue_mask_shift = 0;
            break;
        case 15:
            red_mask_size = 5;  red_mask_shift = 10;
            green_mask_size = 5; green_mask_shift = 5;
            blue_mask_size = 5;  blue_mask_shift = 0;
            break;
        default:                /* 8 bpp: greyscale approximation */
            red_mask_size = 8;  red_mask_shift = 0;
            green_mask_size = 8; green_mask_shift = 0;
            blue_mask_size = 8;  blue_mask_shift = 0;
            break;
        }

        fb.red_mask_size  = red_mask_size;
        fb.red_mask_shift = red_mask_shift;
        fb.green_mask_size  = green_mask_size;
        fb.green_mask_shift = green_mask_shift;
        fb.blue_mask_size  = blue_mask_size;
        fb.blue_mask_shift = blue_mask_shift;
    }

    fb_rloss = (uint8_t)(red_mask_size   ? 8 - red_mask_size   : 8);
    fb_gloss = (uint8_t)(green_mask_size ? 8 - green_mask_size : 8);
    fb_bloss = (uint8_t)(blue_mask_size  ? 8 - blue_mask_size  : 8);

    klog("kernel.HWframe", "mode %ux%u %u bpp pitch %u r%u:%u g%u:%u b%u:%u",
         width, height, bpp, pitch,
         red_mask_size, red_mask_shift,
         green_mask_size, green_mask_shift,
         blue_mask_size, blue_mask_shift);

    fb_ready = 1;
    fb_reset_dirty();

    fb_map_hw();

    framebuffer_buffer_init(pitch, height);
}

void framebuffer_flush(void)
{
    if (!fb_ready || fb_dirty_min >= fb_dirty_max)
        return;

    uint8_t *back = framebuffer_buffer_back();
    if (back) {
        if (fb_dirty_max > fb.height)
            fb_dirty_max = fb.height;

        if (fb_dirty_min == 0 && fb_dirty_max == fb.height) {
            /* whole frame dirty: the visible window is contiguous in the
             * back buffer, so a single copy replaces one memcpy per row. */
            fb_copy_fast(fb.address,
                   back + (uintptr_t)fb_ring_top * fb.pitch,
                   (uintptr_t)fb.height * fb.pitch);
        } else {
            for (uint32_t y = fb_dirty_min; y < fb_dirty_max; y++) {
                fb_copy_fast(fb.address + (uintptr_t)y * fb.pitch,
                       back + (uintptr_t)(fb_ring_top + y) * fb.pitch,
                       fb.pitch);
            }
        }
    }

    /* make the WC stores visible to the display controller */
    asm volatile ("sfence");

    fb_reset_dirty();
}

void framebuffer_putpixel(
    uint32_t x,
    uint32_t y,
    uint32_t color)
{
    if (!fb_ready || x >= fb.width || y >= fb.height)
        return;

    fb_store_pixel(fb_row_ptr(y) + (uintptr_t)x * fb_bytes_pp,
                   fb_pack(color));

    fb_mark_dirty(y);
}

void framebuffer_putpixels(
    uint32_t x,
    uint32_t y,
    const uint32_t *pixels,
    uint32_t count)
{
    if (!fb_ready || y >= fb.height || x >= fb.width || count == 0)
        return;

    if (count > fb.width - x)
        count = fb.width - x;

    uint8_t *dst = fb_row_ptr(y) + (uintptr_t)x * fb_bytes_pp;

    if (fb_bytes_pp == 4 && fb_bloss == 0 && fb_rloss == 0 &&
        fb_gloss == 0 && fb.red_mask_shift == 16 &&
        fb.green_mask_shift == 8 && fb.blue_mask_shift == 0) {
        /* canonical layout at 32bpp: source pixels need no conversion */
        klibc.memcpy(dst, pixels, (uintptr_t)count * 4);
    } else {
        for (uint32_t i = 0; i < count; i++) {
            fb_store_pixel(dst, fb_pack(pixels[i]));
            dst += fb_bytes_pp;
        }
    }

    fb_mark_dirty(y);
}

/*
 * 32bpp word fill: `x`/`w` are pixel coordinates, so a row of u32 words
 * is always 4-byte aligned; only the tail needs a byte-wise fallback.
 * `packed` must already be in hardware format (fb_pack() applied).
 */
static void fb_fill_rect_fast(uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h, uint32_t packed)
{
    uint8_t *base =
        framebuffer_buffer_ready() ? framebuffer_buffer_back() : fb.address;
    uint32_t words_per_row = fb.pitch / 4;
    uint32_t end = x + w;

    for (uint32_t yy = y; yy < y + h; yy++) {
        uint32_t *row = (uint32_t *)base +
                        (fb_ring_top + yy) * words_per_row;
        uint32_t i = x;

        while (i + 4 <= end) {
            row[i] = packed;
            row[i + 1] = packed;
            row[i + 2] = packed;
            row[i + 3] = packed;
            i += 4;
        }
        while (i < end)
            row[i++] = packed;

        fb_mark_dirty(yy);
    }
}

/* 24bpp fill: pixels are not word aligned, fill with byte stores */
static void fb_fill_rect_24(uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h, uint32_t packed)
{
    uint8_t b0 = (uint8_t)packed;
    uint8_t b1 = (uint8_t)(packed >> 8);
    uint8_t b2 = (uint8_t)(packed >> 16);

    for (uint32_t yy = y; yy < y + h; yy++) {
        uint8_t *p = fb_row_ptr(yy) + (uintptr_t)x * 3;
        uint8_t *end = p + (uintptr_t)w * 3;

        while (p < end) {
            p[0] = b0;
            p[1] = b1;
            p[2] = b2;
            p += 3;
        }

        fb_mark_dirty(yy);
    }
}

/* 16/15 bpp fill: u16 stores, two pixels per u32 when aligned */
static void fb_fill_rect_16(uint32_t x, uint32_t y,
                            uint32_t w, uint32_t h, uint32_t packed)
{
    uint16_t v = (uint16_t)packed;

    for (uint32_t yy = y; yy < y + h; yy++) {
        uint16_t *p = (uint16_t *)(void *)
            (fb_row_ptr(yy) + (uintptr_t)x * 2);

        for (uint32_t i = 0; i < w; i++)
            p[i] = v;

        fb_mark_dirty(yy);
    }
}

/* 8bpp fill: plain memset per row segment */
static void fb_fill_rect_8(uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h, uint32_t packed)
{
    for (uint32_t yy = y; yy < y + h; yy++) {
        klibc.memset(fb_row_ptr(yy) + x, (int)(uint8_t)packed, w);
        fb_mark_dirty(yy);
    }
}

void framebuffer_fill_rect(uint32_t x, uint32_t y,
                           uint32_t width, uint32_t height, uint32_t color)
{
    if (!fb_ready || width == 0 || height == 0)
        return;
    if (x >= fb.width || y >= fb.height)
        return;

    if (width  > fb.width  - x) width  = fb.width  - x;
    if (height > fb.height - y) height = fb.height - y;

    switch (fb_bytes_pp) {
    case 4:
        fb_fill_rect_fast(x, y, width, height, fb_pack(color));
        break;
    case 3:
        fb_fill_rect_24(x, y, width, height, fb_pack(color));
        break;
    case 2:
        fb_fill_rect_16(x, y, width, height, fb_pack(color));
        break;
    default:
        fb_fill_rect_8(x, y, width, height, fb_pack(color));
        break;
    }
}

void framebuffer_clear(uint32_t color)
{
    framebuffer_fill_rect(0, 0, fb.width, fb.height, color);
    framebuffer_flush();
}

void framebuffer_scroll_up(uint32_t lines)
{
    if (!fb_ready || lines == 0)
        return;

    if (framebuffer_buffer_ready()) {
        if (lines >= framebuffer_buffer_rows()) {
            fb_ring_top = 0;
            for (uint32_t y = 0; y < fb.height; y++)
                klibc.memset(fb_row_ptr(y), 0x00, fb.pitch);
            fb_mark_all_dirty();
            framebuffer_flush();
            return;
        }
        framebuffer_buffer_free_past(&fb_ring_top, fb.height, lines);
        fb_ring_top += lines;
        fb_mark_all_dirty();
        framebuffer_flush();
        return;
    }

    uint8_t *buf = fb.address;
    uint32_t bytes_per_row = fb.pitch;

    if (lines >= fb.height) {
        klibc.memset(buf, 0x00, (unsigned int)fb.height * bytes_per_row);
        framebuffer_flush();
        return;
    }

    klibc.memmove(buf, buf + (uintptr_t)lines * bytes_per_row,
            (fb.height - lines) * bytes_per_row);

    for (uint32_t y = fb.height - lines; y < fb.height; y++) {
        klibc.memset(buf + y * bytes_per_row, 0x00, bytes_per_row);
    }

    framebuffer_flush();
}

int framebuffer_ready(void)
{
    return fb_ready;
}

u32 framebuffer_width(void)
{
    return fb.width;
}

u32 framebuffer_height(void)
{
    return fb.height;
}

void framebuffer_rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t color)
{
    framebuffer_fill_rect(x, y, width, height, color);
    framebuffer_flush();
}

void fb_print(const char *s)
{
    if (!fb_ready || !s)
        return;

    font_puts(s);
}