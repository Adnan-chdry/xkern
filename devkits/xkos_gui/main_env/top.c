/*
 * top.c - XKOS menu bar (macOS top status bar) with D-Bus routing.
 *
 * The bar hosts, left to right:
 *   - an Apple () menu (About / Sleep / Restart / Shut Down / Lock) whose
 *     items emit D-Bus signals on /com/xkos/Menu, and
 *   - a centered Spotlight search field emitting /com/xkos/Desktop Query,
 * and on the right a status cluster (Wi-Fi, Battery, Volume, Focus, Clock)
 * whose items emit D-Bus "Open" signals on /com/xkos/Status so the DE can
 * pop the matching control-center panel.
 *
 * The bar itself knows nothing about power/volume backends - it only emits.
 */

#include "../widget/widget.h"
#include "../dbus/dbus.h"

static lv_obj_t *g_apple_menu;

static void apple_logo_cb(lv_event_t *e)
{
    (void)e;
    if (lv_obj_has_flag(g_apple_menu, LV_OBJ_FLAG_HIDDEN))
        xkos_menu_show(g_apple_menu);
    else
        xkos_menu_hide(g_apple_menu);
}

static void status_item_cb(lv_event_t *e)
{
    lv_obj_t *item = lv_event_get_target(e);
    const char *name = (const char *)lv_obj_get_user_data(item);
    xkos_dbus_emit("com.xkos.TopBar", XKOS_DBUS_STATUS, "Open", name);
}

static lv_obj_t *status_item(lv_obj_t *bar, const char *glyph,
                             const char *name, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *lbl = lv_label_create(bar);
    lv_label_set_text(lbl, glyph);
    lv_obj_set_style_text_color(lbl, XKOS_LABEL, 0);
    lv_obj_set_style_text_font(lbl, xkos_font(13), 0);
    lv_obj_set_pos(lbl, x, y);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(lbl, (void *)name);
    lv_obj_add_event_cb(lbl, status_item_cb, LV_EVENT_CLICKED, NULL);
    return lbl;
}

lv_obj_t *xkos_topbar_create(lv_obj_t *parent, const char *app_name)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t h  = xkos_px(26);

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, sw, h);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, XKOS_GRAY6, 0);
    lv_obj_set_style_bg_opa(bar, 235, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);

    /* Apple menu (left) - a small rounded-square "app" mark */
    static const char *apple_entries[] = {
        "About This XKOS", "Sleep", "Restart", "Shut Down", "Lock Screen"
    };
    lv_coord_t msz = xkos_px(14);
    lv_obj_t *logo = lv_obj_create(bar);
    lv_obj_set_size(logo, msz, msz);
    lv_obj_set_pos(logo, xkos_px(12), (h - msz) / 2);
    lv_obj_set_style_bg_color(logo, XKOS_LABEL, 0);
    lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(logo, xkos_px(4), 0);
    lv_obj_set_style_border_width(logo, 0, 0);
    lv_obj_clear_flag(logo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(logo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(logo, apple_logo_cb, LV_EVENT_CLICKED, NULL);

    g_apple_menu = xkos_menu_create(bar, xkos_px(8), h + xkos_px(2),
                                    apple_entries,
                                    (int)(sizeof(apple_entries) /
                                          sizeof(apple_entries[0])),
                                    XKOS_DBUS_MENU);

    /* app name */
    xkos_text(bar, app_name ? app_name : "XKOS", XKOS_LABEL, 13,
              xkos_px(30), (h - xkos_px(13)) / 2);

    /* centered Spotlight */
    xkos_search_create(bar, (sw - xkos_px(220)) / 2, (h - xkos_px(28)) / 2,
                       xkos_px(220), XKOS_DBUS_DESKTOP);

    /* hairline separator */
    lv_obj_t *line = lv_obj_create(bar);
    lv_obj_set_size(line, sw, xkos_px(1));
    lv_obj_set_pos(line, 0, h - xkos_px(1));
    lv_obj_set_style_bg_color(line, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(line, 200, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);

    /* status cluster (right) */
    lv_coord_t sy = (h - xkos_px(13)) / 2;
    status_item(bar, "WiFi", "WiFi",  sw - xkos_px(206), sy);
    status_item(bar, "100%", "Battery", sw - xkos_px(168), sy);
    status_item(bar, "Vol",  "Volume",  sw - xkos_px(130), sy);
    status_item(bar, "Fcs",  "Focus",   sw - xkos_px(94),  sy);

    lv_coord_t cx = sw - xkos_px(58);
    xkos_clock_create(bar, cx, sy, XKOS_CLOCK_MENUBAR);

    return bar;
}
