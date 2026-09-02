/*
 * slider.c - XKOS slider widget (macOS NSSlider).
 *
 * A rounded track + accent fill.  Dragging emits a D-Bus signal
 * (bus_path, member, arg = value string) so the DE can drive volume,
 * brightness, backlight, etc.
 */

#include "widget.h"
#include "../dbus/dbus.h"

struct xkos_slider_ctx {
    const char *path;
    const char *member;
};

static void slider_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    struct xkos_slider_ctx *c =
        (struct xkos_slider_ctx *)lv_obj_get_user_data(sl);
    int v = lv_slider_get_value(sl);

    if (c)
        xkos_dbus_emit("com.xkos.Slider", c->path, c->member,
                       xkos_dbus_itoa(v));
}

lv_obj_t *xkos_slider_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t w, int val, int min, int max,
                             const char *bus_path, const char *member)
{
    struct xkos_slider_ctx *c = lv_mem_alloc(sizeof(*c));
    c->path = bus_path;
    c->member = member ? member : "Changed";

    lv_obj_t *sl = lv_slider_create(parent);
    lv_obj_set_pos(sl, x, y);
    lv_obj_set_size(sl, w, xkos_px(14));
    lv_slider_set_range(sl, min, max);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, XKOS_GRAY4, 0);
    lv_obj_set_style_radius(sl, xkos_px(7), 0);
    lv_obj_set_style_bg_color(sl, XKOS_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sl, xkos_px(7), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, XKOS_LABEL, LV_PART_KNOB);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_clear_flag(sl, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_user_data(sl, c);
    lv_obj_add_event_cb(sl, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sl;
}
