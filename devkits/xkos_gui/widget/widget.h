#ifndef XKOS_WIDGET_H
#define XKOS_WIDGET_H

/*
 * widget.h - XKOS GUI widget kit (Cupertino / macOS-inspired, LVGL edition).
 *
 * A small, self-contained AppKit/SwiftUI-flavoured widget layer for the
 * XKERN framebuffer desktop.  Everything is resolution independent: call
 * xkos_scale_init() once after the display is up, then author every widget
 * in *points* and let xkos_px() / xkos_font() map those points to physical
 * pixels (the same logical-point -> device-pixel idea macOS uses).
 *
 * Palette follows the macOS system colours (Dark Mode / "Cupertino").
 */

#include "lvgl.h"
#include "types.h"

/* ===================================================================== */
/*  Colour palette (macOS system colours, dark appearance)               */
/* ===================================================================== */

#define XKOS_ACCENT       lv_color_hex(0x0A84FF)   /* systemBlue        */
#define XKOS_ACCENT_PRESS lv_color_hex(0x0A66CC)   /* pressed accent    */
#define XKOS_LABEL        lv_color_hex(0xFFFFFF)   /* label             */
#define XKOS_LABEL_2      lv_color_hex(0xECECEC)   /* secondaryLabel    */
#define XKOS_TERTIARY     lv_color_hex(0xEBEBF5)   /* tertiaryLabel     */
#define XKOS_QUATERNARY   lv_color_hex(0xEBEBF5)   /* quaternaryLabel   */
#define XKOS_SECONDARY    lv_color_hex(0x8E8E93)   /* systemGray        */
#define XKOS_GRAY2        lv_color_hex(0x636366)   /* systemGray2       */
#define XKOS_GRAY3        lv_color_hex(0x48484A)   /* systemGray3       */
#define XKOS_GRAY4        lv_color_hex(0x3A3A3C)   /* systemGray4       */
#define XKOS_GRAY5        lv_color_hex(0x2C2C2E)   /* systemGray5       */
#define XKOS_GRAY6        lv_color_hex(0x1C1C1E)   /* systemGray6       */

#define XKOS_GREEN        lv_color_hex(0x30D158)   /* systemGreen       */
#define XKOS_RED          lv_color_hex(0xFF453A)   /* systemRed         */
#define XKOS_YELLOW       lv_color_hex(0xFFD60A)   /* systemYellow      */
#define XKOS_ORANGE       lv_color_hex(0xFF9F0A)   /* systemOrange      */
#define XKOS_PURPLE       lv_color_hex(0xBF5AF2)   /* systemPurple      */
#define XKOS_PINK         lv_color_hex(0xFF375F)   /* systemPink        */
#define XKOS_TEAL         lv_color_hex(0x64D2FF)   /* systemTeal        */

#define XKOS_BG           lv_color_hex(0x000000)   /* systemBackground  */
#define XKOS_SURFACE      lv_color_hex(0x2C2C2E)   /* vibrancy panel    */
#define XKOS_SURFACE_2    lv_color_hex(0x1C1C1E)   /* secondary panel   */
#define XKOS_SEPARATOR    lv_color_hex(0x545458)

/* ===================================================================== */
/*  Scaling (logical points -> device pixels)                            */
/* ===================================================================== */

void        xkos_scale_init(void);
float       xkos_scale(void);
lv_coord_t  xkos_px(lv_coord_t base);                 /* scale a point value */
const lv_font_t *xkos_font(int px);                   /* pick a font by point size */

/* ===================================================================== */
/*  Shared surface / text helpers                                        */
/* ===================================================================== */

/* A rounded, translucent "vibrancy" surface (NSVisualEffectView-like). */
lv_obj_t *xkos_surface_create(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                              lv_color_t fill, lv_opa_t opa);

/* Plain text label with colour + point-size font. */
lv_obj_t *xkos_text(lv_obj_t *parent, const char *text, lv_color_t color,
                    int px, lv_coord_t x, lv_coord_t y);

/* Accent (or neutral) push button styled like a macOS NSButton. */
lv_obj_t *xkos_button(lv_obj_t *parent, const char *label, lv_color_t bg,
                      lv_coord_t w, lv_coord_t h);

/* ===================================================================== */
/*  Widgets                                                              */
/* ===================================================================== */

/* --- clock ------------------------------------------------------------ */
#define XKOS_CLOCK_MENUBAR 0   /* compact, for the menu bar            */
#define XKOS_CLOCK_WIDGET  1   /* large tile with date + time          */

lv_obj_t *xkos_clock_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                            int style);

/* --- dialogue (alert) ------------------------------------------------ */
typedef void (*xkos_dialog_cb)(lv_obj_t *dialog, int button_idx, void *ud);

lv_obj_t *xkos_dialog_create(lv_obj_t *parent,
                             const char *title, const char *msg,
                             const char *buttons[], int n,
                             xkos_dialog_cb cb, void *ud);

/* --- notification ----------------------------------------------------- */
lv_obj_t *xkos_notification_create(lv_obj_t *parent,
                                   const char *app, const char *title,
                                   const char *body,
                                   lv_color_t icon_color,
                                   const char *icon_glyph,
                                   uint32_t autodismiss_ms);

/* --- warning ---------------------------------------------------------- */
#define XKOS_WARN_INFO    0
#define XKOS_WARN_WARNING 1
#define XKOS_WARN_ERROR   2

lv_obj_t *xkos_warning_create(lv_obj_t *parent, const char *msg, int kind,
                              lv_coord_t x, lv_coord_t y, lv_coord_t w);

/* --- menu (NSMenu dropdown) ------------------------------------------ */
/* Builds a hidden dropdown of entries; selecting one emits a D-Bus signal
 * (com.xkos.Menu, member "select", arg = entry text) and hides the menu. */
lv_obj_t *xkos_menu_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                           const char **entries, int n, const char *bus_path);
void xkos_menu_show(lv_obj_t *menu);
void xkos_menu_hide(lv_obj_t *menu);

/* --- toggle (NSSwitch) ----------------------------------------------- */
/* On change emits (bus_path, member="Set", arg "1"/"0"). */
lv_obj_t *xkos_toggle_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                             int initial_on, const char *bus_path,
                             const char *member);

/* --- slider (NSSlider) ----------------------------------------------- */
/* On change emits (bus_path, member, arg = value string). */
lv_obj_t *xkos_slider_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t w, int val, int min, int max,
                             const char *bus_path, const char *member);

/* --- popover (vibrancy control popover) ------------------------------ */
lv_obj_t *xkos_popover_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                              lv_coord_t w, lv_coord_t h);
void xkos_popover_show(lv_obj_t *pop);
void xkos_popover_hide(lv_obj_t *pop);

/* --- segmented control (NSSegmentedControl) -------------------------- */
/* Selecting a segment emits (bus_path, member="Select", arg = item text). */
lv_obj_t *xkos_segmented_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                const char **items, int n, int sel,
                                const char *bus_path);

/* --- search field (Spotlight) ---------------------------------------- */
/* On Enter emits (bus_path, member="Query", arg = text). */
lv_obj_t *xkos_search_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t w, const char *bus_path);

/* ===================================================================== */
/*  Main environment (menu bar / dock / app window)                      */
/* ===================================================================== */

/* Menu bar (top). Returns the bar; hosts app name + status clock. */
lv_obj_t *xkos_topbar_create(lv_obj_t *parent, const char *app_name);

/* Dock app descriptor (macOS magnification + running dots). */
struct xkos_dock_app {
    const char *label;
    const char *glyph;
    lv_color_t top;
    lv_color_t bottom;
    void (*launch)(void);
};

lv_obj_t *xkos_dock_create(lv_obj_t *parent,
                           const struct xkos_dock_app *apps, int n);

/* App window with traffic-light controls + content area. */
typedef void (*xkos_app_close_cb)(lv_obj_t *win, void *ud);
lv_obj_t *xkos_app_create(lv_obj_t *parent, const char *title,
                         lv_coord_t w, lv_coord_t h,
                         xkos_app_close_cb cb, void *ud);
lv_obj_t *xkos_app_content(lv_obj_t *win);   /* content area below title bar */

#endif
