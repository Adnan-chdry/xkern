/*
 * warning.c - XKOS warning banner (macOS inline alert style).
 *
 * A horizontal, rounded banner with a coloured glyph on the left and a
 * message to its right, echoing an NSAlert / inline validation message:
 *   XKOS_WARN_INFO     - accent blue  "i"
 *   XKOS_WARN_WARNING  - system yellow "!"
 *   XKOS_WARN_ERROR    - system red    "×"
 * The banner is dismissable via a tap on the close glyph when asked.
 */

#include "widget.h"

struct xkos_warn_ctx {
    lv_obj_t *banner;
};

static void warn_close_cb(lv_event_t *e)
{
    lv_obj_t *x = lv_event_get_target(e);
    lv_obj_t *banner = (lv_obj_t *)lv_obj_get_user_data(x);
    if (banner) lv_obj_del(banner);
}

lv_obj_t *xkos_warning_create(lv_obj_t *parent, const char *msg, int kind,
                              lv_coord_t x, lv_coord_t y, lv_coord_t w)
{
    lv_color_t accent;
    const char *glyph;

    switch (kind) {
    case XKOS_WARN_WARNING: accent = XKOS_YELLOW; glyph = "!"; break;
    case XKOS_WARN_ERROR:   accent = XKOS_RED;    glyph = "×"; break;
    case XKOS_WARN_INFO:
    default:                accent = XKOS_ACCENT; glyph = "i"; break;
    }

    lv_coord_t h = xkos_px(40);

    lv_obj_t *banner = lv_obj_create(parent);
    lv_obj_set_size(banner, w, h);
    lv_obj_set_pos(banner, x, y);
    lv_obj_set_style_bg_color(banner, XKOS_GRAY6, 0);
    lv_obj_set_style_bg_opa(banner, 235, 0);
    lv_obj_set_style_radius(banner, xkos_px(10), 0);
    lv_obj_set_style_border_width(banner, xkos_px(1), 0);
    lv_obj_set_style_border_color(banner, accent, 0);
    lv_obj_set_style_border_opa(banner, 160, 0);
    lv_obj_set_scrollbar_mode(banner, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(banner, LV_OBJ_FLAG_CLICKABLE);

    /* coloured glyph disc */
    lv_coord_t dsz = xkos_px(20);
    lv_obj_t *disc = lv_obj_create(banner);
    lv_obj_set_size(disc, dsz, dsz);
    lv_obj_set_pos(disc, xkos_px(12), (h - dsz) / 2);
    lv_obj_set_style_bg_color(disc, accent, 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(disc, 0, 0);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *g = lv_label_create(disc);
    lv_label_set_text(g, glyph);
    lv_obj_set_style_text_color(g, XKOS_LABEL, 0);
    lv_obj_set_style_text_font(g, xkos_font(13), 0);
    lv_obj_center(g);

    /* message */
    lv_obj_t *m = lv_label_create(banner);
    lv_label_set_text(m, msg ? msg : "");
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(m, w - dsz - xkos_px(44));
    lv_obj_set_style_text_color(m, XKOS_LABEL, 0);
    lv_obj_set_style_text_font(m, xkos_font(12), 0);
    lv_obj_align(m, LV_ALIGN_LEFT_MID, xkos_px(12) + dsz + xkos_px(10), 0);

    /* dismiss glyph */
    lv_obj_t *xbtn = lv_label_create(banner);
    lv_label_set_text(xbtn, "×");
    lv_obj_set_style_text_color(xbtn, XKOS_SECONDARY, 0);
    lv_obj_set_style_text_font(xbtn, xkos_font(16), 0);
    lv_obj_align(xbtn, LV_ALIGN_RIGHT_MID, -xkos_px(12), 0);
    lv_obj_add_flag(xbtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(xbtn, banner);
    lv_obj_add_event_cb(xbtn, warn_close_cb, LV_EVENT_CLICKED, NULL);

    return banner;
}
