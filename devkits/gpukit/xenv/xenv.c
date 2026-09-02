#include "xenv.h"
#include "io.h"
#include "string.h"
#include "stdio.h"
#include "klog.h"
#include "devfs/devfs.h"
#include "tsc.h"

/* ===================================================================== */
/*  LVGL helpers                                                          */
/* ===================================================================== */

/* --- wallpaper ---------------------------------------------------------- */

lv_obj_t *xenv_wallpaper_create(lv_obj_t *parent)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t sh = lv_disp_get_ver_res(NULL);

    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, sw, sh);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x111118), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    /* top-left glow circle */
    lv_obj_t *g1 = lv_obj_create(obj);
    lv_obj_set_size(g1, 440, 440);
    lv_obj_set_pos(g1, -80, -80);
    lv_obj_set_style_bg_color(g1, lv_color_hex(0x1A3D6E), 0);
    lv_obj_set_style_bg_opa(g1, LV_OPA_20, 0);
    lv_obj_set_style_radius(g1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(g1, 0, 0);
    lv_obj_set_scrollbar_mode(g1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(g1, LV_OBJ_FLAG_SCROLLABLE);

    /* bottom-right glow circle */
    lv_obj_t *g2 = lv_obj_create(obj);
    lv_obj_set_size(g2, 360, 360);
    lv_obj_set_pos(g2, sw - 280, sh - 280);
    lv_obj_set_style_bg_color(g2, lv_color_hex(0x1A3D6E), 0);
    lv_obj_set_style_bg_opa(g2, LV_OPA_10, 0);
    lv_obj_set_style_radius(g2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(g2, 0, 0);
    lv_obj_set_scrollbar_mode(g2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(g2, LV_OBJ_FLAG_SCROLLABLE);

    return obj;
}

/* --- icon tile ---------------------------------------------------------- */

lv_obj_t *xenv_icon_tile_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                lv_coord_t w, lv_coord_t h,
                                const char *glyph, const char *label,
                                lv_color_t top, lv_color_t bottom)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_bg_color(obj, top, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, w / 5, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_black(), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(obj, 8, 0);
    lv_obj_set_style_shadow_ofs_y(obj, 3, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_30, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    if (glyph) {
        lv_obj_t *gl = lv_label_create(obj);
        lv_label_set_text(gl, glyph);
        lv_obj_set_style_text_color(gl, lv_color_white(), 0);
        lv_obj_set_style_text_font(gl, &lv_font_montserrat_24, 0);
        lv_obj_center(gl);
    }

    if (label) {
        lv_obj_t *lbl = lv_label_create(obj);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_opa(lbl, LV_OPA_70, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, 4);
    }

    return obj;
}

/* --- dock bar ----------------------------------------------------------- */

lv_obj_t *xenv_dock_bar_create(lv_obj_t *parent)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t sh = lv_disp_get_ver_res(NULL);
    lv_coord_t bw = sw - 120;
    lv_coord_t bh = 52;
    lv_coord_t bx = (sw - bw) / 2;
    lv_coord_t by = sh - bh - 6;

    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, bw, bh);
    lv_obj_set_pos(obj, bx, by);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x282830), 0);
    lv_obj_set_style_bg_opa(obj, 214, 0);
    lv_obj_set_style_radius(obj, bh / 2, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x6E6E78), 0);
    lv_obj_set_style_border_opa(obj, 120, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    return obj;
}

/* --- menu bar ----------------------------------------------------------- */

#define MENUBAR_H 28

static lv_obj_t *g_dropdown;

static void menubar_click_cb(lv_event_t *e)
{
    lv_obj_t *bar = lv_event_get_target(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t mp;
    lv_indev_get_point(indev, &mp);

    lv_area_t area;
    lv_obj_get_coords(bar, &area);

    if (mp.x < area.x1 + 200) {
        if (g_dropdown && lv_obj_has_flag(g_dropdown, LV_OBJ_FLAG_HIDDEN))
            lv_obj_clear_flag(g_dropdown, LV_OBJ_FLAG_HIDDEN);
        else if (g_dropdown)
            lv_obj_add_flag(g_dropdown, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (g_dropdown && !lv_obj_has_flag(g_dropdown, LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(g_dropdown, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *xenv_menubar_create(lv_obj_t *parent, const char *title,
                              const char *items[], int count)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, sw, MENUBAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, XENV_MENUBAR, 0);
    lv_obj_set_style_bg_opa(bar, 235, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* bottom line */
    lv_obj_t *line = lv_obj_create(bar);
    lv_obj_set_size(line, sw, 1);
    lv_obj_set_pos(line, 0, MENUBAR_H - 1);
    lv_obj_set_style_bg_color(line, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(line, 235, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mark = lv_obj_create(bar);
    lv_obj_set_size(mark, 14, 14);
    lv_obj_set_pos(mark, 12, 7);
    lv_obj_set_style_bg_color(mark, lv_color_hex(0xE8E8EE), 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(mark, 4, 0);
    lv_obj_set_style_border_width(mark, 0, 0);
    lv_obj_clear_flag(mark, LV_OBJ_FLAG_SCROLLABLE);

    /* app title */
    lv_obj_t *title_lbl = lv_label_create(bar);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(title_lbl, 30, (MENUBAR_H - 14) / 2);

    /* status: battery + clock */
    lv_obj_t *bat = lv_label_create(bar);
    lv_label_set_text(bat, "100%");
    lv_obj_set_style_text_color(bat, lv_color_hex(0xCFCFD4), 0);
    lv_obj_set_style_text_font(bat, &lv_font_montserrat_12, 0);
    lv_obj_align(bat, LV_ALIGN_TOP_RIGHT, -120, (MENUBAR_H - 12) / 2);

    char timebuf[16];
    u32 up = (u32)(tsc_ms() / 1000);
    snprintf(timebuf, sizeof(timebuf), "%u:%02u", up / 60, up % 60);

    lv_obj_t *clock = lv_label_create(bar);
    lv_label_set_text(clock, timebuf);
    lv_obj_set_style_text_color(clock, lv_color_white(), 0);
    lv_obj_set_style_text_font(clock, &lv_font_montserrat_12, 0);
    lv_obj_align(clock, LV_ALIGN_TOP_RIGHT, -10, (MENUBAR_H - 12) / 2);

    /* dropdown (hidden) */
    g_dropdown = xenv_menubar_dropdown_create(parent, items, count,
                                             MENUBAR_H + 4);

    lv_obj_add_event_cb(bar, menubar_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    return bar;
}

lv_obj_t *xenv_menubar_dropdown_create(lv_obj_t *parent,
                                       const char *items[], int count,
                                       lv_coord_t below_y)
{
    lv_coord_t iw = 240;
    lv_coord_t item_h = 26;
    lv_coord_t total_h = count * item_h + 8;

    lv_obj_t *dd = lv_obj_create(parent);
    lv_obj_set_size(dd, iw, total_h);
    lv_obj_set_pos(dd, 8, below_y);
    lv_obj_set_style_bg_color(dd, lv_color_hex(0x32323A), 0);
    lv_obj_set_style_bg_opa(dd, 240, 0);
    lv_obj_set_style_radius(dd, 8, 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_border_color(dd, lv_color_hex(0x5A5A64), 0);
    lv_obj_set_style_border_opa(dd, 120, 0);
    lv_obj_set_style_shadow_width(dd, 14, 0);
    lv_obj_set_style_shadow_color(dd, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(dd, LV_OPA_50, 0);
    lv_obj_set_scrollbar_mode(dd, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(dd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dd, LV_OBJ_FLAG_HIDDEN);

    int i;
    for (i = 0; i < count; i++) {
        lv_obj_t *item = lv_obj_create(dd);
        lv_obj_set_size(item, iw - 12, item_h - 4);
        lv_obj_set_pos(item, 6, 4 + i * item_h);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 5, 0);
        lv_obj_set_scrollbar_mode(item, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, items[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xE2E2E8), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(lbl, 10, (item_h - 4 - 14) / 2);
    }

    return dd;
}

/* ===================================================================== */
/*  Platform helpers (unchanged from original)                             */
/* ===================================================================== */

/* --- power --------------------------------------------------------------- */

void xenv_reboot(void)
{
    int i;

    klog("xenv", "rebooting...");
    for (i = 0; i < 16; i++)
        (void)inb(0x64);
    outb(0x64, 0xFE);
    for (;;)
        asm volatile ("cli; hlt");
}

void xenv_poweroff(void)
{
    klog("xenv", "powering off...");
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    for (;;)
        asm volatile ("cli; hlt");
}

/* --- embedded install payload -------------------------------------------- */

extern char install_payload_start[], install_payload_end[];

int xenv_payload_present(void)
{
    return (u32)(unsigned long)install_payload_end >
           (u32)(unsigned long)install_payload_start;
}

u32 xenv_payload_size(void)
{
    return (u32)((u32)(unsigned long)install_payload_end -
                 (u32)(unsigned long)install_payload_start);
}

const u8 *xenv_payload_data(void)
{
    return (const u8 *)(unsigned long)install_payload_start;
}

/* --- disk enumeration ---------------------------------------------------- */

int xenv_disk_count(void)
{
    int i, n = 0;

    for (i = 0; i < DEVFS_MAX_DEVICES; i++) {
        struct devfs_device *d = devfs_get(i);
        if (!d) break;
        if (d->type == DEVFS_BLOCK_DEV) n++;
    }
    return n;
}

int xenv_disk_get(int i, const char **name, const char **model, u32 *mb)
{
    int k, n = 0;

    for (k = 0; k < DEVFS_MAX_DEVICES; k++) {
        struct devfs_device *d = devfs_get(k);
        if (!d) break;
        if (d->type != DEVFS_BLOCK_DEV) continue;
        if (n++ == i) {
            if (name) *name = d->name;
            if (model) *model = d->model;
            if (mb) *mb = d->block_count;
            return 0;
        }
    }
    return -1;
}

/* --- helpers ------------------------------------------------------------- */

lv_coord_t xenv_px(lv_coord_t base)
{
    return base;
}


/*
    simplified api
    needed v 2.2 
*/