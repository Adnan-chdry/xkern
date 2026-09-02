#ifndef XENV_H
#define XENV_H

/*
 * xenv.h - shared desktop-environment layer for XKERN GPUkit (LVGL edition).
 *
 * Common helpers used by the macOS-styled recovery menu, the desktop
 * environment and the installer wizard: wallpaper, icons, dock chrome,
 * reboot/power handling, embedded install payload access and disk
 * enumeration.
 *
 * All GUI helpers now return lv_obj_t* parents and create LVGL objects
 * inside them.  Callers place the returned objects on the screen.
 */

#include "lvgl.h"
#include "types.h"

/* --- palette (macOS-flavoured) ------------------------------------------- */

#define XENV_TEXT         lv_color_hex(0xE8E8EC)
#define XENV_WHITE        lv_color_hex(0xFFFFFF)
#define XENV_MENUBAR      lv_color_hex(0x2A2A30)
#define XENV_LIGHT        lv_color_hex(0x323239)
#define XENV_LIGHT_BORDER lv_color_hex(0x4A4A54)
#define XENV_GRAY         lv_color_hex(0x8E8E93)
#define XENV_BLUE         lv_color_hex(0x0A7BFF)
#define XENV_BLUE_DARK    lv_color_hex(0x0655B8)
#define XENV_GREEN        lv_color_hex(0x28C840)
#define XENV_RED          lv_color_hex(0xFF5F57)
#define XENV_YELLOW       lv_color_hex(0xFEBC2E)
#define XENV_ORANGE       lv_color_hex(0xFF9F0A)
#define XENV_PURPLE       lv_color_hex(0xAF5CF0)
#define XENV_CYAN         lv_color_hex(0x32AAD8)

#define XENV_BG           lv_color_hex(0x111111)
#define XENV_PANEL        lv_color_hex(0x222226)
#define XENV_PANEL_BORDER lv_color_hex(0x3E3E44)
#define XENV_PANEL_TITLE  lv_color_hex(0x1E1E1E)
#define XENV_SEL_BG       lv_color_hex(0x143C6E)
#define XENV_SEL_BORDER   lv_color_hex(0x2E66B4)

/* --- wallpaper ------------------------------------------------------------ */

/* Full-screen wallpaper object with a dark background and corner glows. */
lv_obj_t *xenv_wallpaper_create(lv_obj_t *parent);

/* --- icons / dock ---------------------------------------------------------- */

/* macOS-style rounded-square icon tile. */
lv_obj_t *xenv_icon_tile_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                lv_coord_t w, lv_coord_t h,
                                const char *glyph, const char *label,
                                lv_color_t top, lv_color_t bottom);

/* Translucent dock bar at the bottom of the screen. */
lv_obj_t *xenv_dock_bar_create(lv_obj_t *parent);

/* --- menu bar -------------------------------------------------------------- */

/* Menubar with a left-side app title and right-side status items.
 * Returns the bar object.  The returned object has an event callback
 * (user_data = title_label) that toggles the dropdown on click. */
lv_obj_t *xenv_menubar_create(lv_obj_t *parent, const char *title,
                              const char *items[], int count);

/* Create a dropdown popup below the menubar. Returns a hidden object
 * that can be shown/hidden with lv_obj_clear_flag / lv_obj_add_flag. */
lv_obj_t *xenv_menubar_dropdown_create(lv_obj_t *parent,
                                       const char *items[], int count,
                                       lv_coord_t below_y);

/* --- power ----------------------------------------------------------------- */

void xenv_reboot(void);
void xenv_poweroff(void);

/* --- embedded install payload ---------------------------------------------- */

int  xenv_payload_present(void);
u32  xenv_payload_size(void);
const u8 *xenv_payload_data(void);

/* --- disk enumeration ------------------------------------------------------ */

int xenv_disk_count(void);
int xenv_disk_get(int i, const char **name, const char **model, u32 *mb);

/* --- helpers --------------------------------------------------------------- */

lv_coord_t xenv_px(lv_coord_t base);

#endif
