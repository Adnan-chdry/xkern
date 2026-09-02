/*
 * xkos_ui.c - XKOS GUI shared core: display scaling, vibrancy surface and
 * the small NSView-flavoured primitives the widgets are built from.
 *
 * Scaling model (mirrors macOS display scaling): the framebuffer has a
 * physical pixel resolution, but we author the UI in logical *points*.  The
 * scale factor is the ratio of the physical horizontal resolution to a
 * 1280-point reference width, clamped to a sane 1.0x..3.0x range.  Every
 * widget therefore looks identical on a 1024x768 panel and a 2560x1600 one,
 * just sharper.
 */

#include "widget.h"

/* ===================================================================== */
/*  Scaling state                                                         */
/* ===================================================================== */

static float g_scale  = 1.0f;
static int   g_inited = 0;

void xkos_scale_init(void)
{
    lv_coord_t w = lv_disp_get_hor_res(NULL);
    lv_coord_t h = lv_disp_get_ver_res(NULL);

    if (w <= 0 || h <= 0) {            /* display not ready yet */
        g_scale = 1.0f;
        g_inited = 1;
        return;
    }

    /* Reference logical width = 1280pt (a 13" MacBook). */
    float s = (float)w / 1280.0f;
    if (s < 1.0f) s = 1.0f;
    if (s > 3.0f) s = 3.0f;
    g_scale = s;
    g_inited = 1;
}

float xkos_scale(void)
{
    if (!g_inited) xkos_scale_init();
    return g_scale;
}

lv_coord_t xkos_px(lv_coord_t base)
{
    if (!g_inited) xkos_scale_init();
    return (lv_coord_t)((float)base * g_scale + 0.5f);
}

/* Pick the closest built-in Montserrat size for a requested point size. */
const lv_font_t *xkos_font(int px)
{
    int s = (int)((float)px * xkos_scale() + 0.5f);

    /* Only the sizes enabled in lv_conf.h are reachable. */
    if (s <= 12) return &lv_font_montserrat_12;
    if (s <= 14) return &lv_font_montserrat_14;
    if (s <= 16) return &lv_font_montserrat_16;
    if (s <= 18) return &lv_font_montserrat_18;
    if (s <= 20) return &lv_font_montserrat_20;
    if (s <= 24) return &lv_font_montserrat_24;
    if (s <= 26) return &lv_font_montserrat_26;
    if (s <= 28) return &lv_font_montserrat_28;
    if (s <= 32) return &lv_font_montserrat_32;
    return &lv_font_montserrat_48;
}

/* ===================================================================== */
/*  Vibrancy surface (NSVisualEffectView)                                */
/* ===================================================================== */

lv_obj_t *xkos_surface_create(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                              lv_color_t fill, lv_opa_t opa)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, fill, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_radius(obj, xkos_px(12), 0);
    lv_obj_set_style_border_width(obj, xkos_px(1), 0);
    lv_obj_set_style_border_color(obj, XKOS_SEPARATOR, 0);
    lv_obj_set_style_border_opa(obj, 90, 0);
    lv_obj_set_style_shadow_width(obj, xkos_px(24), 0);
    lv_obj_set_style_shadow_ofs_y(obj, xkos_px(8), 0);
    lv_obj_set_style_shadow_color(obj, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(obj, 45, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

/* ===================================================================== */
/*  Text + button primitives                                             */
/* ===================================================================== */

lv_obj_t *xkos_text(lv_obj_t *parent, const char *text, lv_color_t color,
                    int px, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl, xkos_font(px), 0);
    lv_obj_set_pos(lbl, x, y);
    return lbl;
}

lv_obj_t *xkos_button(lv_obj_t *parent, const char *label, lv_color_t bg,
                      lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_color(btn, lv_color_mix(lv_color_black(), bg, 25),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, xkos_px(7), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, xkos_px(6), 0);
    lv_obj_set_style_shadow_opa(btn, 25, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, XKOS_LABEL, 0);
    lv_obj_set_style_text_font(lbl, xkos_font(13), 0);
    lv_obj_center(lbl);
    return btn;
}
