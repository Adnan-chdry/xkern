/*
 * menu.c - XKOS menu widget (macOS NSMenu dropdown).
 *
 * A hidden vibrancy dropdown of selectable rows.  Picking a row emits a
 * D-Bus signal on the supplied path (member "select", arg = row text) and
 * hides the menu, exactly like a menu action routing through the bus.
 */

#include "widget.h"
#include "../dbus/dbus.h"

static void menu_row_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    const char *bus_path = (const char *)lv_obj_get_user_data(row);
    const char *label = lv_label_get_text(lv_obj_get_child(row, 0));

    xkos_dbus_emit("com.xkos.Menu", bus_path, "select", label);
    xkos_menu_hide(lv_obj_get_parent(row));
}

lv_obj_t *xkos_menu_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                           const char **entries, int n, const char *bus_path)
{
    lv_coord_t w  = xkos_px(220);
    lv_coord_t rh = xkos_px(28);
    lv_coord_t h  = (lv_coord_t)n * rh + xkos_px(8);

    lv_obj_t *menu = xkos_surface_create(parent, w, h, XKOS_SURFACE, 240);
    lv_obj_set_pos(menu, x, y);
    lv_obj_add_flag(menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(menu, LV_OBJ_FLAG_CLICKABLE);

    int i;
    for (i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_create(menu);
        lv_obj_set_size(row, w - xkos_px(12), rh - xkos_px(4));
        lv_obj_set_pos(row, xkos_px(6), xkos_px(4) + (lv_coord_t)i * rh);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row, XKOS_GRAY4, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, xkos_px(5), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(row, (void *)bus_path);
        lv_obj_add_event_cb(row, menu_row_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, entries[i]);
        lv_obj_set_style_text_color(lbl, XKOS_LABEL, 0);
        lv_obj_set_style_text_font(lbl, xkos_font(13), 0);
        lv_obj_set_pos(lbl, xkos_px(10), (rh - xkos_px(4) - xkos_px(13)) / 2);
    }

    return menu;
}

void xkos_menu_show(lv_obj_t *menu) { lv_obj_clear_flag(menu, LV_OBJ_FLAG_HIDDEN); }
void xkos_menu_hide(lv_obj_t *menu) { lv_obj_add_flag(menu, LV_OBJ_FLAG_HIDDEN); }
