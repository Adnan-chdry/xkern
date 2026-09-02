/*
 * search.c - XKOS search field (macOS Spotlight).
 *
 * A rounded one-line text field with a placeholder.  Pressing Enter emits a
 * D-Bus signal (bus_path, member="Query", arg = text) so the DE can run a
 * search / launch action.
 */

#include "widget.h"
#include "../dbus/dbus.h"

struct xkos_search_ctx {
    const char *path;
};

static void search_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    struct xkos_search_ctx *c =
        (struct xkos_search_ctx *)lv_obj_get_user_data(ta);
    const char *txt = lv_textarea_get_text(ta);

    if (c && txt && *txt)
        xkos_dbus_emit("com.xkos.Search", c->path, "Query", txt);
}

lv_obj_t *xkos_search_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t w, const char *bus_path)
{
    struct xkos_search_ctx *c = lv_mem_alloc(sizeof(*c));
    c->path = bus_path;

    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_pos(ta, x, y);
    lv_obj_set_size(ta, w, xkos_px(28));
    lv_textarea_set_one_line(ta, 1);
    lv_textarea_set_placeholder_text(ta, "Spotlight  Search");
    lv_obj_set_style_bg_color(ta, XKOS_GRAY6, 0);
    lv_obj_set_style_bg_opa(ta, 210, 0);
    lv_obj_set_style_radius(ta, xkos_px(14), 0);
    lv_obj_set_style_border_width(ta, xkos_px(1), 0);
    lv_obj_set_style_border_color(ta, XKOS_SEPARATOR, 0);
    lv_obj_set_style_border_opa(ta, 120, 0);
    lv_obj_set_style_text_color(ta, XKOS_LABEL, 0);
    lv_obj_set_style_text_font(ta, xkos_font(13), 0);
    lv_obj_set_style_pad_left(ta, xkos_px(12), 0);
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_user_data(ta, c);
    lv_obj_add_event_cb(ta, search_cb, LV_EVENT_READY, NULL);
    return ta;
}
