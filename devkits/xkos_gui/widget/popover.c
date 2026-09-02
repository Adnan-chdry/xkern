/*
 * popover.c - XKOS popover (macOS control-center / menu popover).
 *
 * A vibrancy panel anchored somewhere on screen, hidden by default.  The
 * caller fills it with toggles / sliders / segmented controls and shows it
 * (e.g. when a status-bar item emits a D-Bus "Open" signal).
 */

#include "widget.h"

lv_obj_t *xkos_popover_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                              lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *pop = xkos_surface_create(parent, w, h, XKOS_SURFACE, 240);
    lv_obj_set_pos(pop, x, y);
    lv_obj_add_flag(pop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pop, LV_OBJ_FLAG_CLICKABLE);
    return pop;
}

void xkos_popover_show(lv_obj_t *pop) { lv_obj_clear_flag(pop, LV_OBJ_FLAG_HIDDEN); }
void xkos_popover_hide(lv_obj_t *pop) { lv_obj_add_flag(pop, LV_OBJ_FLAG_HIDDEN); }
