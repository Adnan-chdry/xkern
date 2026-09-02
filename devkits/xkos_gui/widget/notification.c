/*
 * notification.c - XKOS notification widget (macOS banner style).
 *
 * A translucent vibrancy card pinned to the top-right of the screen,
 * modelled on a macOS notification: a rounded app icon, a bold title, an
 * optional body line and a close affordance.  Optionally auto-dismisses
 * after autodismiss_ms (0 = sticky).  Slides in from the right edge via a
 * short LVGL animation, exactly like Notification Centre.
 */

#include "widget.h"

struct xkos_notif_ctx {
    lv_obj_t  *card;
    lv_timer_t *timer;
};

static void notif_close(lv_obj_t *card)
{
    /* shrink out to the right, then delete */
    lv_coord_t x = lv_obj_get_x(card);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, card);
    lv_anim_set_values(&a, x, lv_disp_get_hor_res(NULL));
    lv_anim_set_time(&a, 220);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_set_ready_cb(&a, (lv_anim_ready_cb_t)lv_obj_del);
    lv_anim_start(&a);
}

static void notif_close_cb(lv_event_t *e)
{
    notif_close(lv_event_get_target(e));
}

static void notif_autodismiss_cb(lv_timer_t *t)
{
    struct xkos_notif_ctx *c = (struct xkos_notif_ctx *)t->user_data;
    lv_timer_del(t);
    if (c->card) notif_close(c->card);
    lv_mem_free(c);
}

lv_obj_t *xkos_notification_create(lv_obj_t *parent,
                                   const char *app, const char *title,
                                   const char *body,
                                   lv_color_t icon_color,
                                   const char *icon_glyph,
                                   uint32_t autodismiss_ms)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);

    lv_coord_t w  = xkos_px(320);
    lv_coord_t h  = body && *body ? xkos_px(84) : xkos_px(64);
    lv_coord_t x  = sw - w - xkos_px(16);
    lv_coord_t y  = xkos_px(16);

    lv_obj_t *card = xkos_surface_create(parent, w, h, XKOS_SURFACE, 230);
    lv_obj_set_pos(card, sw, y);          /* start off-screen, animate in */
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    /* app icon (rounded square in the supplied accent) */
    lv_coord_t isz = xkos_px(36);
    lv_obj_t *icon = lv_obj_create(card);
    lv_obj_set_size(icon, isz, isz);
    lv_obj_set_pos(icon, xkos_px(14), xkos_px(14));
    lv_obj_set_style_bg_color(icon, icon_color, 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(icon, isz / 5, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *gl = lv_label_create(icon);
    lv_label_set_text(gl, icon_glyph ? icon_glyph : "•");
    lv_obj_set_style_text_color(gl, XKOS_LABEL, 0);
    lv_obj_set_style_text_font(gl, xkos_font(16), 0);
    lv_obj_center(gl);

    /* app name (small, secondary) */
    xkos_text(card, app ? app : "XKOS", XKOS_SECONDARY, 11,
              xkos_px(58), xkos_px(14));

    /* title (bold-ish, primary) */
    xkos_text(card, title ? title : "", XKOS_LABEL, 13,
              xkos_px(58), xkos_px(30));

    /* body (wrapped, secondary) */
    if (body && *body) {
        lv_obj_t *bl = lv_label_create(card);
        lv_label_set_text(bl, body);
        lv_label_set_long_mode(bl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(bl, w - xkos_px(72));
        lv_obj_set_style_text_color(bl, XKOS_LABEL_2, 0);
        lv_obj_set_style_text_font(bl, xkos_font(12), 0);
        lv_obj_set_pos(bl, xkos_px(58), xkos_px(50));
    }

    /* close affordance */
    lv_obj_t *xbtn = lv_label_create(card);
    lv_label_set_text(xbtn, "×");
    lv_obj_set_style_text_color(xbtn, XKOS_SECONDARY, 0);
    lv_obj_set_style_text_font(xbtn, xkos_font(18), 0);
    lv_obj_align(xbtn, LV_ALIGN_TOP_RIGHT, -xkos_px(10), xkos_px(6));
    lv_obj_add_flag(xbtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(xbtn, notif_close_cb, LV_EVENT_CLICKED, NULL);

    /* slide in */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, card);
    lv_anim_set_values(&a, sw, x);
    lv_anim_set_time(&a, 240);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_start(&a);

    if (autodismiss_ms) {
        struct xkos_notif_ctx *c =
            (struct xkos_notif_ctx *)lv_mem_alloc(sizeof(*c));
        c->card = card;
        c->timer = lv_timer_create(notif_autodismiss_cb, autodismiss_ms, c);
    }

    return card;
}
