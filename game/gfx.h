#ifndef GAME_GFX_H
#define GAME_GFX_H

#include "types.h"

#define GFX_TRANSPARENT 0xFFFFFFFF

#define GFX_RGB(r, g, b) \
    ((u32)((((u32)(r) & 0xFF) << 16) | (((u32)(g) & 0xFF) << 8) | ((u32)(b) & 0xFF)))

#define GFX_BLACK   GFX_RGB(0x00, 0x00, 0x00)
#define GFX_WHITE   GFX_RGB(0xFF, 0xFF, 0xFF)
#define GFX_RED     GFX_RGB(0xC8, 0x20, 0x20)
#define GFX_GREEN   GFX_RGB(0x20, 0xC8, 0x20)
#define GFX_BLUE    GFX_RGB(0x20, 0x20, 0xC8)
#define GFX_YELLOW  GFX_RGB(0xE8, 0xC8, 0x20)
#define GFX_ORANGE  GFX_RGB(0xE8, 0x80, 0x20)
#define GFX_CYAN    GFX_RGB(0x20, 0xC8, 0xC8)
#define GFX_MAGENTA GFX_RGB(0xC8, 0x20, 0xC8)

u32  gfx_width(void);
u32  gfx_height(void);
void gfx_init(void);

void gfx_clear(u32 color);
void gfx_flush(void);

void gfx_pixel(s32 x, s32 y, u32 color);
void gfx_rect(s32 x, s32 y, u32 w, u32 h, u32 color);
void gfx_rect_outline(s32 x, s32 y, u32 w, u32 h, u32 color);
void gfx_line(s32 x0, s32 y0, s32 x1, s32 y1, u32 color);
void gfx_circle(s32 cx, s32 cy, s32 r, u32 color);
void gfx_circle_outline(s32 cx, s32 cy, s32 r, u32 color);
void gfx_triangle(s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, u32 color);

void gfx_text(s32 x, s32 y, const char *s, u32 fg, u32 bg);
u32  gfx_text_width(const char *s);

/*
 * 1-bpp bitmap sprite.  Each row is `w` pixels stored with the MSB of
 * each byte first; rows are packed (row stride = (w + 7) / 8 bytes).
 * Set pixels are drawn in `fg`; if `transparent` is zero the clear
 * pixels are drawn in `bg`, otherwise they are skipped.
 */
void gfx_sprite(s32 x, s32 y, u32 w, u32 h, const u8 *bits, u32 fg, u32 bg,
                int transparent);

#endif
