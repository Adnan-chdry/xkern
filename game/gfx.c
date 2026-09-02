/*
 * gfx.c - 2D raster primitives for the xkern game engine.
 *
 * All drawing goes through framebuffer_putpixel(), which writes into
 * the HWframe back buffer (when double buffering is active) and tracks
 * dirty rows.  Nothing reaches the screen until gfx_flush(), so a game
 * can compose a whole frame and swap it in one shot.
 */
#include "gfx.h"
#include "IOGraphicsFamily/fb.h"
#include "IOGraphicsFamily/font.h"

static u32 g_width;
static u32 g_height;

static void clamp_rect(s32 x, s32 y, s32 *w, s32 *h)
{
    if (x < 0) {
        *w += x;
        x = 0;
    }
    if (y < 0) {
        *h += y;
        y = 0;
    }
    if (*w < 0) *w = 0;
    if (*h < 0) *h = 0;
    if (x + *w > (s32)g_width)  *w = (s32)g_width - x;
    if (y + *h > (s32)g_height) *h = (s32)g_height - y;
}

void gfx_init(void)
{
    g_width  = framebuffer_width();
    g_height = framebuffer_height();
}

u32 gfx_width(void)
{
    return g_width;
}

u32 gfx_height(void)
{
    return g_height;
}

void gfx_clear(u32 color)
{
    framebuffer_clear(color);
}

void gfx_flush(void)
{
    framebuffer_flush();
}

void gfx_pixel(s32 x, s32 y, u32 color)
{
    framebuffer_putpixel((u32)x, (u32)y, color);
}

void gfx_rect(s32 x, s32 y, u32 w, u32 h, u32 color)
{
    s32 ww = (s32)w, hh = (s32)h;

    if (x >= (s32)g_width || y >= (s32)g_height)
        return;

    clamp_rect(x, y, &ww, &hh);
    if (ww <= 0 || hh <= 0)
        return;

    framebuffer_fill_rect((u32)x, (u32)y, (u32)ww, (u32)hh, color);
}

void gfx_rect_outline(s32 x, s32 y, u32 w, u32 h, u32 color)
{
    if (h == 0 || w == 0)
        return;

    gfx_rect(x, y, w, 1, color);
    gfx_rect(x, y + (s32)h - 1, w, 1, color);
    gfx_rect(x, y, 1, h, color);
    gfx_rect(x + (s32)w - 1, y, 1, h, color);
}

void gfx_line(s32 x0, s32 y0, s32 x1, s32 y1, u32 color)
{
    s32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
    s32 dy = y1 > y0 ? y1 - y0 : y0 - y1;
    s32 sx = x0 < x1 ? 1 : -1;
    s32 sy = y0 < y1 ? 1 : -1;
    s32 err = dx - dy;

    for (;;) {
        framebuffer_putpixel((u32)x0, (u32)y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        {
            s32 e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x0 += sx;
            }
            if (e2 < dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
}

void gfx_circle_outline(s32 cx, s32 cy, s32 r, u32 color)
{
    s32 x = r, y = 0, err = 0;

    if (r < 0)
        return;

    while (x >= y) {
        framebuffer_putpixel((u32)(cx + x), (u32)(cy + y), color);
        framebuffer_putpixel((u32)(cx + y), (u32)(cy + x), color);
        framebuffer_putpixel((u32)(cx - y), (u32)(cy + x), color);
        framebuffer_putpixel((u32)(cx - x), (u32)(cy + y), color);
        framebuffer_putpixel((u32)(cx - x), (u32)(cy - y), color);
        framebuffer_putpixel((u32)(cx - y), (u32)(cy - x), color);
        framebuffer_putpixel((u32)(cx + y), (u32)(cy - x), color);
        framebuffer_putpixel((u32)(cx + x), (u32)(cy - y), color);

        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) {
            x--;
            err += 1 - 2 * x;
        }
    }
}

void gfx_circle(s32 cx, s32 cy, s32 r, u32 color)
{
    s32 y;

    if (r < 0)
        return;

    for (y = -r; y <= r; y++) {
        s32 dy = y * y;
        s32 span = 0;

        while (span * span + dy <= r * r)
            span++;

        span--;
        if (span >= 0)
            gfx_rect(cx - span, cy + y, (u32)(2 * span + 1), 1, color);
    }
}

void gfx_triangle(s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, u32 color)
{
    s32 t;

    /* sort vertices by y (bubble, only 3 items) */
    if (y0 > y1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (y1 > y2) { t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    if (y0 > y1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }

    for (s32 y = y0; y <= y2; y++) {
        s32 xa, xb, lo, hi;

        if (y1 == y0) {
            /* flat top: edges (0,2) and (1,2) */
            xa = x0;
            xb = x1;
        } else if (y == y2) {
            xa = x2;
            xb = x2;
        } else {
            xa = x0 + (y - y0) * (x2 - x0) / (y2 - y0);
            xb = (y < y1)
                     ? x0 + (y - y0) * (x1 - x0) / (y1 - y0)
                     : x1 + (y - y1) * (x2 - x1) / (y2 - y1);
        }

        if (xa < xb) { lo = xa; hi = xb; }
        else         { lo = xb; hi = xa; }

        if (hi >= lo)
            gfx_rect(lo, y, (u32)(hi - lo + 1), 1, color);
    }
}

void gfx_text(s32 x, s32 y, const char *s, u32 fg, u32 bg)
{
    u32 advance = font_glyph_width();
    u32 i = 0;

    if (!s)
        return;

    while (s[i]) {
        font_draw_glyph((u32)(x + (s32)(i * advance)), (u32)y, s[i], fg, bg);
        i++;
    }
}

u32 gfx_text_width(const char *s)
{
    u32 len = 0;

    if (!s)
        return 0;
    while (s[len])
        len++;
    return len * font_glyph_width();
}

void gfx_sprite(s32 x, s32 y, u32 w, u32 h, const u8 *bits, u32 fg, u32 bg,
                int transparent)
{
    u32 stride = (w + 7) / 8;

    for (u32 yy = 0; yy < h; yy++) {
        for (u32 xx = 0; xx < w; xx++) {
            int set = (bits[yy * stride + (xx >> 3)] & (0x80 >> (xx & 7))) != 0;

            if (set) {
                framebuffer_putpixel((u32)(x + (s32)xx), (u32)(y + (s32)yy), fg);
            } else if (!transparent) {
                framebuffer_putpixel((u32)(x + (s32)xx), (u32)(y + (s32)yy), bg);
            }
        }
    }
}
