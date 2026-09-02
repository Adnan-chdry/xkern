/*
 * app_body.c - XKOS application window (macOS NSWindow style).
 *
 * A rounded, shadowed window with a title bar carrying the three
 * "traffic-light" controls (close / minimise / zoom) on the left and a
 * centred title.  xkos_app_content() returns the client area below the
 * title bar so apps can drop their own widgets in.  Closing the window
 * fires the caller's callback.
 */

#include "../widget/widget.h"

struct xkos_app_ctx {
    xkos_app_close_cb cb;
    void             *ud;
};

static void app_close_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *win = (lv_obj_t *)lv_obj_get_user_data(btn);
    struct xkos_app_ctx *c =
        (struct xkos_app_ctx *)lv_obj_get_user_data(win);

    if (c && c->cb)
        c->cb(win, c->ud);
    lv_obj_del(win);
    if (c) lv_mem_free(c);
}

static lv_obj_t *traffic_light(lv_obj_t *bar, lv_color_t col,
                               lv_coord_t x, lv_coord_t y, lv_obj_t *win,
                               struct xkos_app_ctx *c)
{
    lv_coord_t sz = xkos_px(12);
    lv_obj_t *b = lv_btn_create(bar);
    lv_obj_set_size(b, sz, sz);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, col, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(b, win);
    lv_obj_add_event_cb(b, app_close_cb, LV_EVENT_CLICKED, NULL);
    (void)c;
    return b;
}

lv_obj_t *xkos_app_create(lv_obj_t *parent, const char *title,
                         lv_coord_t w, lv_coord_t h,
                         xkos_app_close_cb cb, void *ud)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);

    w = xkos_px((int)w);
    h = xkos_px((int)h);
    lv_coord_t x = (sw - w) / 2;
    lv_coord_t y = xkos_px(60);

    lv_obj_t *win = lv_obj_create(parent);
    lv_obj_set_size(win, w, h);
    lv_obj_set_pos(win, x, y);
    lv_obj_set_style_bg_color(win, XKOS_SURFACE_2, 0);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(win, xkos_px(11), 0);
    lv_obj_set_style_border_width(win, xkos_px(1), 0);
    lv_obj_set_style_border_color(win, XKOS_SEPARATOR, 0);
    lv_obj_set_style_border_opa(win, 120, 0);
    lv_obj_set_style_shadow_width(win, xkos_px(30), 0);
    lv_obj_set_style_shadow_ofs_y(win, xkos_px(10), 0);
    lv_obj_set_style_shadow_color(win, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(win, LV_OPA_50, 0);
    lv_obj_set_scrollbar_mode(win, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(win, LV_OBJ_FLAG_CLICKABLE);

    struct xkos_app_ctx *c =
        (struct xkos_app_ctx *)lv_mem_alloc(sizeof(*c));
    c->cb = cb; c->ud = ud;
    lv_obj_set_user_data(win, c);

    /* title bar */
    lv_coord_t th = xkos_px(28);
    lv_obj_t *bar = lv_obj_create(win);
    lv_obj_set_size(bar, w, th);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, XKOS_GRAY6, 0);
    lv_obj_set_style_bg_opa(bar, 240, 0);
    lv_obj_set_style_radius(bar, xkos_px(11), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);

    /* traffic lights */
    lv_coord_t ly = (th - xkos_px(12)) / 2;
    traffic_light(bar, XKOS_RED,    xkos_px(12), ly, win, c);
    traffic_light(bar, XKOS_YELLOW, xkos_px(30), ly, win, c);
    traffic_light(bar, XKOS_GREEN,  xkos_px(48), ly, win, c);

    /* title */
    lv_obj_t *tl = lv_label_create(bar);
    lv_label_set_text(tl, title ? title : "");
    lv_obj_set_style_text_color(tl, XKOS_LABEL, 0);
    lv_obj_set_style_text_font(tl, xkos_font(13), 0);
    lv_obj_align(tl, LV_ALIGN_TOP_MID, 0, ly - xkos_px(1));

    return win;
}

lv_obj_t *xkos_app_content(lv_obj_t *win)
{
    /* caller should place children relative to win; we just clear the
     * scrollable flag and return the window itself as the content parent.
     * The 28pt title bar is purely decorative (drawn on top). */
    lv_coord_t th = xkos_px(28);
    lv_obj_t *pad = lv_obj_create(win);
    lv_obj_set_size(pad, lv_obj_get_width(win) - xkos_px(2),
                    lv_obj_get_height(win) - th - xkos_px(2));
    lv_obj_set_pos(pad, xkos_px(1), th + xkos_px(1));
    lv_obj_set_style_bg_color(pad, XKOS_SURFACE_2, 0);
    lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad, 0, 0);
    lv_obj_set_style_radius(pad, 0, 0);
    lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(pad, LV_SCROLLBAR_MODE_OFF);
    lv_obj_move_background(pad);
    return pad;
}
