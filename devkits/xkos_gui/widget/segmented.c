/*
 * segmented.c - XKOS segmented control (macOS NSSegmentedControl).
 *
 * A row of equal segments; the selected one is highlighted in the accent.
 * Selecting emits a D-Bus signal (bus_path, member="Select", arg = text).
 */

#include "widget.h"
#include "../dbus/dbus.h"

struct xkos_seg_ctx {
    const char *path;
    lv_obj_t   *seg[];
};

static void seg_cb(lv_event_t *e)
{
    lv_obj_t *seg = lv_event_get_target(e);
    struct xkos_seg_ctx *c = (struct xkos_seg_ctx *)lv_obj_get_user_data(seg);
    lv_obj_t *parent = lv_obj_get_parent(seg);
    const char *txt = lv_label_get_text(lv_obj_get_child(seg, 0));

    /* unhighlight siblings, highlight this */
    uint32_t i, cnt = lv_obj_get_child_cnt(parent);
    for (i = 0; i < cnt; i++) {
        lv_obj_t *ch = lv_obj_get_child(parent, i);
        lv_obj_set_style_bg_opa(ch, LV_OPA_TRANSP, 0);
    }
    lv_obj_set_style_bg_color(seg, XKOS_GRAY4, 0);
    lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);

    xkos_dbus_emit("com.xkos.Segmented", c->path, "Select", txt);
}

lv_obj_t *xkos_segmented_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                const char **items, int n, int sel,
                                const char *bus_path)
{
    lv_coord_t sw = xkos_px(64);
    lv_coord_t sh = xkos_px(26);
    lv_coord_t w  = (lv_coord_t)n * sw;

    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, w, sh);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_color(box, XKOS_GRAY6, 0);
    lv_obj_set_style_bg_opa(box, 200, 0);
    lv_obj_set_style_radius(box, xkos_px(6), 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);

    struct xkos_seg_ctx *c =
        lv_mem_alloc(sizeof(*c) + (size_t)n * sizeof(lv_obj_t *));
    c->path = bus_path;

    int i;
    for (i = 0; i < n; i++) {
        lv_coord_t sx = (lv_coord_t)i * sw;
        lv_obj_t *seg = lv_obj_create(box);
        lv_obj_set_size(seg, sw, sh);
        lv_obj_set_pos(seg, sx, 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(seg, xkos_px(5), 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(seg, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *lbl = lv_label_create(seg);
        lv_label_set_text(lbl, items[i]);
        lv_obj_set_style_text_color(lbl, XKOS_LABEL, 0);
        lv_obj_set_style_text_font(lbl, xkos_font(12), 0);
        lv_obj_center(lbl);

        if (i == sel) {
            lv_obj_set_style_bg_color(seg, XKOS_GRAY4, 0);
            lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        }

        c->seg[i] = seg;
        lv_obj_set_user_data(seg, c);
        lv_obj_add_event_cb(seg, seg_cb, LV_EVENT_CLICKED, NULL);
    }

    return box;
}
