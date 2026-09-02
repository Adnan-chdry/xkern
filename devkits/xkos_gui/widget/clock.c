/*
 * clock.c - XKOS clock widget (macOS menu-bar / dashboard style).
 *
 * Two presentations:
 *   XKOS_CLOCK_MENUBAR - a compact, right-aligned status clock for the
 *                        top menu bar ("Thu  9:41 AM"), refreshed each
 *                        second by an LVGL timer.
 *   XKOS_CLOCK_WIDGET  - a large vibrancy tile with the weekday/date on
 *                        top and the time beneath, like a Notification
 *                        Centre / Dashboard clock.
 *
 * Time is derived from lv_tick_get() (the LVGL millisecond clock); the
 * kernel has no RTC wired here, so we expose elapsed time from boot as a
 * stand-in wall clock starting at 09:41 (the classic Apple keynote time).
 */

#include "widget.h"
#include "stdio.h"

#define XKOS_EPOCH_OFFSET  (9 * 3600 + 41 * 60)   /* boot "wall clock" 09:41 */

struct xkos_clock_ctx {
    lv_obj_t *time;
    lv_obj_t *date;
    int       style;
};

static const char *g_wday[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static uint32_t clock_now_sec(void)
{
    /* LVGL tick is ms since init; treat as a monotonic wall clock. */
    uint64_t ms = (uint64_t)lv_tick_get();
    uint32_t total = XKOS_EPOCH_OFFSET + (uint32_t)(ms / 1000U);
    return total % 86400U;            /* seconds of the (fake) day */
}

static void clock_timer_cb(lv_timer_t *t)
{
    struct xkos_clock_ctx *c = (struct xkos_clock_ctx *)t->user_data;
    uint32_t s = clock_now_sec();
    uint32_t hh = s / 3600U;
    uint32_t mm = (s / 60U) % 60U;
    uint32_t ss = s % 60U;

    if (c->style == XKOS_CLOCK_MENUBAR) {
        char buf[16];
        int ampm = (hh >= 12) ? 1 : 0;
        uint32_t h12 = hh % 12U; if (h12 == 0) h12 = 12;
        snprintf(buf, sizeof(buf), "%u:%02u %s",
                 h12, mm, ampm ? "PM" : "AM");
        lv_label_set_text(c->time, buf);
    } else {
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%02u:%02u:%02u", hh, mm, ss);
        lv_label_set_text(c->time, tbuf);

        uint32_t day_idx = (uint32_t)(lv_tick_get() / 1000U) % 7U;
        char dbuf[32];
        snprintf(dbuf, sizeof(dbuf), "%s  %02u:%02u", g_wday[day_idx], mm, ss);
        lv_label_set_text(c->date, dbuf);
    }
}

lv_obj_t *xkos_clock_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                            int style)
{
    struct xkos_clock_ctx *c =
        (struct xkos_clock_ctx *)lv_mem_alloc(sizeof(*c));
    lv_memset(c, 0, sizeof(*c));
    c->style = style;

    if (style == XKOS_CLOCK_MENUBAR) {
        c->time = lv_label_create(parent);
        lv_obj_set_style_text_color(c->time, XKOS_LABEL, 0);
        lv_obj_set_style_text_font(c->time, xkos_font(12), 0);
        lv_obj_set_pos(c->time, x, y);
        lv_label_set_text(c->time, "9:41 AM");
    } else {
        lv_coord_t w = xkos_px(220);
        lv_coord_t h = xkos_px(150);
        lv_obj_t *tile = xkos_surface_create(parent, w, h,
                                             XKOS_SURFACE, 235);
        lv_obj_set_pos(tile, x, y);

        c->date = xkos_text(tile, "Thu  00:00",
                            XKOS_SECONDARY, 13, xkos_px(16), xkos_px(20));
        c->time = xkos_text(tile, "09:41:00",
                            XKOS_LABEL, 40, xkos_px(16), xkos_px(48));
    }

    lv_timer_create(clock_timer_cb, 1000, c);
    return (style == XKOS_CLOCK_MENUBAR) ? c->time
                                         : lv_obj_get_parent(c->time);
}
