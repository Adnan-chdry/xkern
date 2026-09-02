/*
 * dock.c - XKOS dock (macOS Dock with magnification).
 *
 * A centred, translucent rounded bar riding the bottom edge.  Each app is a
 * rounded-square tile with a glyph and a running indicator dot.  Tapping a
 * tile magnifies it (the signature Dock "genie" pop) and invokes the app's
 * launch callback.  Tiles are scaled in device pixels via xkos_px so the
 * Dock keeps its proportions at any resolution.
 */

#include "../widget/widget.h"

struct xkos_dock_tile {
    lv_obj_t *obj;
    lv_coord_t base;
    void (*launch)(void);
};

static void dock_tile_cb(lv_event_t *e)
{
    lv_obj_t *tile = lv_event_get_target(e);
    struct xkos_dock_tile *t =
        (struct xkos_dock_tile *)lv_obj_get_user_data(tile);

    if (e->code == LV_EVENT_PRESSED) {
        lv_coord_t big = (lv_coord_t)(t->base * xkos_scale() * 1.35f);
        lv_obj_set_size(tile, big, big);
        lv_obj_set_style_radius(tile, big / 5, 0);
    } else if (e->code == LV_EVENT_RELEASED ||
               e->code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_size(tile, t->base, t->base);
        lv_obj_set_style_radius(tile, t->base / 5, 0);
        if (t->launch) t->launch();
    }
}

lv_obj_t *xkos_dock_create(lv_obj_t *parent,
                           const struct xkos_dock_app *apps, int n)
{
    if (n <= 0) return NULL;

    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t sh = lv_disp_get_ver_res(NULL);

    lv_coord_t base = xkos_px(52);
    lv_coord_t gap  = xkos_px(14);
    lv_coord_t pad  = xkos_px(14);

    lv_coord_t n_w = (lv_coord_t)n * base + (lv_coord_t)(n - 1) * gap;
    lv_coord_t bw = n_w + pad * 2;
    lv_coord_t bh = base + pad * 2;
    lv_coord_t bx = (sw - bw) / 2;
    lv_coord_t by = sh - bh - xkos_px(6);

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, bw, bh);
    lv_obj_set_pos(bar, bx, by);
    lv_obj_set_style_bg_color(bar, XKOS_GRAY5, 0);
    lv_obj_set_style_bg_opa(bar, 214, 0);
    lv_obj_set_style_radius(bar, bh / 2, 0);
    lv_obj_set_style_border_width(bar, xkos_px(1), 0);
    lv_obj_set_style_border_color(bar, XKOS_GRAY2, 0);
    lv_obj_set_style_border_opa(bar, 120, 0);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t x0 = bx + pad;
    lv_coord_t cy = by + pad + base / 2;
    int i;
    for (i = 0; i < n; i++) {
        lv_coord_t cx = x0 + (lv_coord_t)i * (base + gap) + base / 2;

        lv_obj_t *tile = lv_obj_create(parent);
        lv_obj_set_size(tile, base, base);
        lv_obj_set_pos(tile, cx - base / 2, cy - base / 2);
        lv_obj_set_style_bg_color(tile, apps[i].top, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tile, base / 5, 0);
        lv_obj_set_style_border_width(tile, xkos_px(1), 0);
        lv_obj_set_style_border_color(tile, lv_color_black(), 0);
        lv_obj_set_style_border_opa(tile, 50, 0);
        lv_obj_set_style_shadow_width(tile, xkos_px(8), 0);
        lv_obj_set_style_shadow_ofs_y(tile, xkos_px(3), 0);
        lv_obj_set_style_shadow_opa(tile, LV_OPA_30, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *g = lv_label_create(tile);
        lv_label_set_text(g, apps[i].glyph);
        lv_obj_set_style_text_color(g, XKOS_LABEL, 0);
        lv_obj_set_style_text_font(g, xkos_font(20), 0);
        lv_obj_center(g);

        struct xkos_dock_tile *t =
            (struct xkos_dock_tile *)lv_mem_alloc(sizeof(*t));
        t->obj = tile; t->base = base; t->launch = apps[i].launch;
        lv_obj_set_user_data(tile, t);

        lv_obj_add_event_cb(tile, dock_tile_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(tile, dock_tile_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(tile, dock_tile_cb, LV_EVENT_PRESS_LOST, NULL);

        /* running indicator dot */
        lv_coord_t dsz = xkos_px(4);
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_set_size(dot, dsz, dsz);
        lv_obj_set_pos(dot, cx - dsz / 2, cy + base / 2 + xkos_px(4));
        lv_obj_set_style_bg_color(dot, XKOS_LABEL, 0);
        lv_obj_set_style_bg_opa(dot, 180, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

        /* label below */
        xkos_text(parent, apps[i].label, XKOS_LABEL, 11,
                  cx - xkos_px(24), cy + base / 2 + xkos_px(10));
    }

    return bar;
}
