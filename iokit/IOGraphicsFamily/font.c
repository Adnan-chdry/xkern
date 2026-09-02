/*
 * font - console font engine (logic shared by all font sets)
 */
#include "font.h"
#include "fb.h"

struct font_set {
    const u8 *glyphs;
    u32 glyph_w;        /* drawn pixels per row (bits used in each byte) */
    u32 cell_w;         /* advance per char incl. spacing */
    u32 h;
};

static const struct font_set sets[] = {
    [FONT_9X8]  = { font9x8_glyphs,  8, 9, 8  },
    [FONT_6X12] = { font6x12_glyphs, 6, 6, 12 },
};
#define FONT_SET_COUNT (sizeof(sets) / sizeof(sets[0]))

static int active = FONT_9X8;

static u32 columns;
static u32 rows;
static u32 cursor_x;
static u32 cursor_y;

/* default palette: white on black */
#define FG_DEFAULT 0x00FFFFFF
#define BG_DEFAULT 0x00000000

void font_select(int set)
{
    if (set >= 0 && set < (int)FONT_SET_COUNT)
        active = set;
}

int font_current(void)
{
    return active;
}

void font_init(struct multiboot_info *mbi)
{
    columns = mbi->framebuffer_width  / sets[active].cell_w;
    rows    = mbi->framebuffer_height / sets[active].h;

    cursor_x = 0;
    cursor_y = 0;
}

static void font_scroll(void)
{
    framebuffer_scroll_up(sets[active].h);
    cursor_y = rows - 1;
}

static void font_draw(char c)
{
    if ((u8)c < 0x20 || (u8)c > 0x7F)
        return;

    const struct font_set *f = &sets[active];
    const u8 *glyph = f->glyphs[(u8)c - 0x20];

    u32 px = cursor_x * f->cell_w;
    u32 py = cursor_y * f->h;

    u32 row[16];

    for (u32 y = 0; y < f->h; y++) {
        for (u32 x = 0; x < f->glyph_w; x++)
            row[x] = (glyph[y] & (1 << (7 - x))) ? FG_DEFAULT : BG_DEFAULT;
        for (u32 x = f->glyph_w; x < f->cell_w; x++)
            row[x] = BG_DEFAULT;            /* spacing column */

        framebuffer_putpixels(px, py + y, row, f->cell_w);
    }
}

void font_putc(char c)
{
    if (rows == 0 || columns == 0)
        return;

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        framebuffer_flush();
    }
    else if (c == '\r') {
        cursor_x = 0;
    }
    else if ((u8)c >= 0x20 && (u8)c <= 0x7F) {
        font_draw(c);
        cursor_x++;
    }

    if (cursor_x >= columns) {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= rows)
        font_scroll();
}

void font_puts(const char *s)
{
    while (*s) {
        font_putc(*s);
        s++;
    }
}

void font_clear(u32 color)
{
    framebuffer_clear(color);

    cursor_x = 0;
    cursor_y = 0;
}

void font_draw_glyph(u32 x, u32 y, char c, u32 fg, u32 bg)
{
    if ((u8)c < 0x20 || (u8)c > 0x7F)
        return;

    const struct font_set *f = &sets[active];
    const u8 *glyph = f->glyphs[(u8)c - 0x20];

    int transparent = (bg == 0xFFFFFFFF);

    for (u32 yy = 0; yy < f->h; yy++) {
        u8 g = glyph[yy];

        if (transparent) {
            for (u32 xx = 0; xx < f->glyph_w; xx++) {
                if (g & (1 << (7 - xx)))
                    framebuffer_putpixel(x + xx, y + yy, fg);
            }
        } else {
            u32 row[16];

            for (u32 xx = 0; xx < f->glyph_w; xx++)
                row[xx] = (g & (1 << (7 - xx))) ? fg : bg;
            for (u32 xx = f->glyph_w; xx < f->cell_w; xx++)
                row[xx] = bg;

            framebuffer_putpixels(x, y + yy, row, f->cell_w);
        }
    }
}

u32 font_glyph_width(void)
{
    return sets[active].cell_w;
}

u32 font_glyph_height(void)
{
    return sets[active].h;
}

u32 font_columns(void)
{
    return columns;
}

u32 font_rows(void)
{
    return rows;
}
