/*
 * dialouge.c - XKOS alert/dialogue widget (macOS NSAlert style).
 *
 * A centred modal panel with a bold title, a wrapped body message and a
 * row of buttons bottom-right.  The first (right-most in macOS, but we
 * keep array order) button is the emphasized "default" action rendered in
 * the system accent; the rest are subtle text buttons.  Dismissal invokes
 * the caller callback with the chosen button index.
 *
 * Note: source file keeps the project's historical "dialouge" spelling.
 */

#include "widget.h"

struct xkos_dialog_ctx {
    xkos_dialog_cb cb;
    void          *ud;
    lv_obj_t      *panel;
};

static void dialog_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    struct xkos_dialog_ctx *c =
        (struct xkos_dialog_ctx *)lv_obj_get_user_data(btn);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (c && c->cb)
        c->cb(c->panel, idx, c->ud);

    if (c->panel)
        lv_obj_del(c->panel);
    lv_mem_free(c);
}

lv_obj_t *xkos_dialog_create(lv_obj_t *parent,
                             const char *title, const char *msg,
                             const char *buttons[], int n,
                             xkos_dialog_cb cb, void *ud)
{
    if (n <= 0) n = 1;
    if (!buttons) {
        static const char *def[1] = { "OK" };
        buttons = def;
    }

    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t sh = lv_disp_get_ver_res(NULL);

    lv_coord_t w = xkos_px(320);
    lv_coord_t h = xkos_px(150) + xkos_px(20) * (n > 2 ? 1 : 0);
    lv_coord_t x = (sw - w) / 2;
    lv_coord_t y = (sh - h) / 2;

    lv_obj_t *panel = xkos_surface_create(parent, w, h, XKOS_SURFACE_2, 245);
    lv_obj_set_pos(panel, x, y);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);

    struct xkos_dialog_ctx *c =
        (struct xkos_dialog_ctx *)lv_mem_alloc(sizeof(*c));
    lv_memset(c, 0, sizeof(*c));
    c->cb = cb; c->ud = ud; c->panel = panel;

    /* Title (bold-ish, larger) */
    xkos_text(panel, title ? title : "Alert", XKOS_LABEL, 15,
              xkos_px(20), xkos_px(18));

    /* Message (wrapped, secondary colour) */
    lv_obj_t *body = lv_label_create(panel);
    lv_label_set_text(body, msg ? msg : "");
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, w - xkos_px(40));
    lv_obj_set_style_text_color(body, XKOS_LABEL_2, 0);
    lv_obj_set_style_text_font(body, xkos_font(12), 0);
    lv_obj_set_pos(body, xkos_px(20), xkos_px(46));

    /* Buttons row, bottom-right */
    lv_coord_t bw = xkos_px(78);
    lv_coord_t bh = xkos_px(28);
    lv_coord_t by = h - bh - xkos_px(16);
    lv_coord_t bx = w - xkos_px(20) - bw;

    int i;
    for (i = 0; i < n; i++) {
        lv_obj_t *btn = lv_btn_create(panel);
        lv_obj_set_size(btn, bw, bh);
        lv_obj_set_pos(btn, bx, by);
        lv_obj_set_style_radius(btn, xkos_px(7), 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        if (i == 0) {
            /* emphasised default action */
            lv_obj_set_style_bg_color(btn, XKOS_ACCENT, 0);
            lv_obj_set_style_bg_color(btn, XKOS_ACCENT_PRESS,
                                      LV_PART_MAIN | LV_STATE_PRESSED);
        } else {
            lv_obj_set_style_bg_color(btn, XKOS_GRAY4, 0);
            lv_obj_set_style_bg_color(btn, XKOS_GRAY3,
                                      LV_PART_MAIN | LV_STATE_PRESSED);
        }

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, buttons[i]);
        lv_obj_set_style_text_color(lbl, XKOS_LABEL, 0);
        lv_obj_set_style_text_font(lbl, xkos_font(13), 0);
        lv_obj_center(lbl);

        lv_obj_set_user_data(btn, c);
        lv_obj_add_event_cb(btn, dialog_btn_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        bx -= bw + xkos_px(12);     /* lay out right-to-left (macOS order) */
    }

    return panel;
}
