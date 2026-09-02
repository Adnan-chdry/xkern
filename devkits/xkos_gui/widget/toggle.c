/*
 * toggle.c - XKOS toggle widget (macOS NSSwitch).
 *
 * A rounded on/off switch.  Flipping it emits a D-Bus signal
 * (bus_path, member="Set", arg "1"/"0") so the DE can reflect state
 * (e.g. Wi-Fi, Focus, Bluetooth) without the control knowing the backend.
 */

#include "widget.h"
#include "../dbus/dbus.h"

struct xkos_toggle_ctx {
    const char *path;
    const char *member;
};

static void toggle_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    struct xkos_toggle_ctx *c =
        (struct xkos_toggle_ctx *)lv_obj_get_user_data(sw);
    int on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (c)
        xkos_dbus_emit("com.xkos.Toggle", c->path, c->member, on ? "1" : "0");
}

lv_obj_t *xkos_toggle_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                             int initial_on, const char *bus_path,
                             const char *member)
{
    struct xkos_toggle_ctx *c = lv_mem_alloc(sizeof(*c));
    c->path = bus_path;
    c->member = member ? member : "Set";

    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_pos(sw, x, y);
    lv_obj_set_size(sw, xkos_px(42), xkos_px(24));
    lv_obj_set_style_bg_color(sw, XKOS_GRAY4, 0);
    lv_obj_set_style_bg_color(sw, XKOS_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(sw, xkos_px(12), 0);
    lv_obj_set_style_border_width(sw, 0, 0);
    lv_obj_set_style_pad_all(sw, xkos_px(2), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, XKOS_LABEL, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_user_data(sw, c);

    if (initial_on)
        lv_obj_add_state(sw, LV_STATE_CHECKED);

    lv_obj_add_event_cb(sw, toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}
