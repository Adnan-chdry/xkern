/*
 * font - selectable console font engine (IOGraphicsFamily)
 *
 * Two sets are compiled in; rendering, cursor and scroll logic are
 * shared. The active set is chosen with font_select() before font_init().
 *
 *   FONT_9X8  - classic 8x8 bitmaps (9 px cell incl. spacing column)
 *   FONT_6X12 - Spleen 6x12 (BSD-2-Clause, (c) Frederic Cambus)
 */
#pragma once
#include "types.h"
#include "multiboot.h"

enum {
    FONT_9X8  = 0,
    FONT_6X12 = 1,
};

void font_select(int set);
int  font_current(void);

void font_init(struct multiboot_info *mbi);

void font_putc(char c);
void font_puts(const char *s);
void font_clear(u32 color);

void font_draw_glyph(u32 x, u32 y, char c, u32 fg, u32 bg);
u32  font_glyph_width(void);
u32  font_glyph_height(void);

u32  font_columns(void);
u32  font_rows(void);

extern const u8 font9x8_glyphs[96][8];
extern const u8 font6x12_glyphs[96][12];
