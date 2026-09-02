/*
 * xkos_de.c - XKOS desktop environment (Cupertino-flavoured, D-Bus driven).
 *
 * Stands up a full macOS-style session on the GPUkit/LVGL compositor:
 *   - animated wallpaper, menu bar (Apple menu + status cluster + Spotlight),
 *   - a magnifying dock with running apps,
 *   - desktop icons, a control-center popover, lock screen and alerts.
 *
 * Every interactive surface talks to the DE only through the in-kernel
 * D-Bus bus (see dbus.h): the top bar emits /com/xkos/Menu, /com/xkos/Status
 * and /com/xkos/Desktop signals; the DE's handlers perform the real work
 * (open popovers, reboot, show alerts, etc.).  This keeps the widgets
 * completely backend-agnostic - exactly the AppKit/NSApp separation.
 *
 * Run loop is the same pattern as gpukit's xdesktop_run(): pump the input
 * port, run LVGL timers, pace to ~60fps, exit on ESC.
 */

#include "xkos_de.h"
#include "../widget/widget.h"
#include "../dbus/dbus.h"
#include "lv_port.h"
#include "gpukit/lv_console.h"
#include "IOGraphicsFamily/fb.h"
#include "input.h"
#include "tsc.h"
#include "klog.h"
#include "io.h"
#include "stdio.h"
#include "string.h"

/* ===================================================================== */
/*  State                                                                 */
/* ===================================================================== */

static lv_obj_t *g_scr;
static lv_obj_t *g_cc;        /* control-center popover          */
static lv_obj_t *g_lock;      /* lock-screen overlay             */
static lv_obj_t *g_body;      /* current front app content area  */

/* ===================================================================== */
/*  Power (local ACPI-style outb helpers)                                 */
/* ===================================================================== */

static void de_reboot(void)
{
    klog("xkos.de", "reboot");
    int i;
    for (i = 0; i < 16; i++) (void)inb(0x64);
    outb(0x64, 0xFE);
    for (;;) asm volatile ("cli; hlt");
}

static void de_poweroff(void)
{
    klog("xkos.de", "power off");
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    for (;;) asm volatile ("cli; hlt");
}

/* ===================================================================== */
/*  Wallpaper + desktop chrome                                            */
/* ===================================================================== */

static void de_wallpaper(void)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t sh = lv_disp_get_ver_res(NULL);

    lv_obj_set_style_bg_color(g_scr, lv_color_hex(0x111118), 0);
    lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);

    lv_obj_t *g1 = lv_obj_create(g_scr);
    lv_obj_set_size(g1, xkos_px(440), xkos_px(440));
    lv_obj_set_pos(g1, -xkos_px(80), -xkos_px(80));
    lv_obj_set_style_bg_color(g1, lv_color_hex(0x1A3D6E), 0);
    lv_obj_set_style_bg_opa(g1, 20, 0);
    lv_obj_set_style_radius(g1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(g1, 0, 0);
    lv_obj_clear_flag(g1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *g2 = lv_obj_create(g_scr);
    lv_obj_set_size(g2, xkos_px(360), xkos_px(360));
    lv_obj_set_pos(g2, sw - xkos_px(280), sh - xkos_px(280));
    lv_obj_set_style_bg_color(g2, lv_color_hex(0x1A3D6E), 0);
    lv_obj_set_style_bg_opa(g2, 10, 0);
    lv_obj_set_style_radius(g2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(g2, 0, 0);
    lv_obj_clear_flag(g2, LV_OBJ_FLAG_SCROLLABLE);
}

static void desktop_icon_cb(lv_event_t *e)
{
    (void)e;
}

static void de_icons(void)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t x = sw - xkos_px(60);
    lv_coord_t y1 = xkos_px(90);

    /* Hard Disk */
    lv_obj_t *hd = lv_obj_create(g_scr);
    lv_obj_set_size(hd, xkos_px(64), xkos_px(64));
    lv_obj_set_pos(hd, x - xkos_px(32), y1 - xkos_px(32));
    lv_obj_set_style_bg_color(hd, XKOS_ACCENT, 0);
    lv_obj_set_style_bg_opa(hd, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hd, xkos_px(13), 0);
    lv_obj_set_style_border_width(hd, 0, 0);
    lv_obj_clear_flag(hd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hd, desktop_icon_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hgl = lv_label_create(hd);
    lv_label_set_text(hgl, "HD");
    lv_obj_set_style_text_color(hgl, XKOS_LABEL, 0);
    lv_obj_set_style_text_font(hgl, xkos_font(16), 0);
    lv_obj_center(hgl);

    xkos_text(g_scr, "Hard Disk", XKOS_LABEL, 11,
              x - xkos_px(30), y1 + xkos_px(38));

    /* Trash */
    lv_obj_t *tr = lv_obj_create(g_scr);
    lv_obj_set_size(tr, xkos_px(64), xkos_px(64));
    lv_obj_set_pos(tr, x - xkos_px(32), y1 + xkos_px(120) - xkos_px(32));
    lv_obj_set_style_bg_color(tr, XKOS_GRAY3, 0);
    lv_obj_set_style_bg_opa(tr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tr, xkos_px(13), 0);
    lv_obj_set_style_border_width(tr, 0, 0);
    lv_obj_clear_flag(tr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tr, desktop_icon_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *tgl = lv_label_create(tr);
    lv_label_set_text(tgl, "x");
    lv_obj_set_style_text_color(tgl, XKOS_LABEL, 0);
    lv_obj_set_style_text_font(tgl, xkos_font(16), 0);
    lv_obj_center(tgl);

    xkos_text(g_scr, "Trash", XKOS_LABEL, 11,
              x - xkos_px(20), y1 + xkos_px(120) + xkos_px(38));
}

/* ===================================================================== */
/*  Control center (popover)                                              */
/* ===================================================================== */

static void de_build_cc(void)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t w  = xkos_px(240);
    lv_coord_t h  = xkos_px(256);
    lv_coord_t x  = sw - w - xkos_px(16);
    lv_coord_t y  = xkos_px(26) + xkos_px(4);

    g_cc = xkos_popover_create(g_scr, x, y, w, h);

    xkos_text(g_cc, "Control Center", XKOS_LABEL, 14, xkos_px(12), xkos_px(10));

    xkos_text(g_cc, "Wi-Fi",  XKOS_LABEL_2, 12, xkos_px(12), xkos_px(40));
    xkos_toggle_create(g_cc, w - xkos_px(54), xkos_px(38), 1,
                       XKOS_DBUS_CC, "WiFi");
    xkos_text(g_cc, "Bluetooth", XKOS_LABEL_2, 12, xkos_px(12), xkos_px(72));
    xkos_toggle_create(g_cc, w - xkos_px(54), xkos_px(70), 0,
                       XKOS_DBUS_CC, "Bluetooth");
    xkos_text(g_cc, "Focus", XKOS_LABEL_2, 12, xkos_px(12), xkos_px(104));
    xkos_toggle_create(g_cc, w - xkos_px(54), xkos_px(102), 0,
                       XKOS_DBUS_CC, "Focus");

    xkos_text(g_cc, "Volume", XKOS_LABEL_2, 12, xkos_px(12), xkos_px(140));
    xkos_slider_create(g_cc, xkos_px(12), xkos_px(160), w - xkos_px(24),
                       60, 0, 100, XKOS_DBUS_CC, "Volume");

    xkos_text(g_cc, "Display", XKOS_LABEL_2, 12, xkos_px(12), xkos_px(190));
    static const char *disp[] = { "Light", "Dark", "Auto" };
    xkos_segmented_create(g_cc, xkos_px(12), xkos_px(208),
                          disp, 3, 2, XKOS_DBUS_CC);
}

/* ===================================================================== */
/*  Lock screen                                                           */
/* ===================================================================== */

static void de_build_lock(void)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t sh = lv_disp_get_ver_res(NULL);

    g_lock = lv_obj_create(g_scr);
    lv_obj_set_size(g_lock, sw, sh);
    lv_obj_set_pos(g_lock, 0, 0);
    lv_obj_set_style_bg_color(g_lock, XKOS_BG, 0);
    lv_obj_set_style_bg_opa(g_lock, 235, 0);
    lv_obj_set_style_radius(g_lock, 0, 0);
    lv_obj_set_style_border_width(g_lock, 0, 0);
    lv_obj_clear_flag(g_lock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_lock, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_lock, LV_OBJ_FLAG_CLICKABLE);

    xkos_text(g_lock, "Locked", XKOS_LABEL, 28,
              (sw - xkos_px(80)) / 2, sh / 2 - xkos_px(20));

    xkos_search_create(g_lock, (sw - xkos_px(240)) / 2, sh / 2 + xkos_px(20),
                       xkos_px(240), XKOS_DBUS_DESKTOP);
}

static void de_lock(void) { lv_obj_clear_flag(g_lock, LV_OBJ_FLAG_HIDDEN); }
static void de_unlock(void) { lv_obj_add_flag(g_lock, LV_OBJ_FLAG_HIDDEN); }

/* ===================================================================== */
/*  App windows (dock launches)                                           */
/* ===================================================================== */

static void de_app_close_cb(lv_obj_t *win, void *ud)
{
    (void)ud;
    g_body = NULL;
    (void)win;
    klog("xkos.de", "app closed");
}

static void de_open_app(const char *title)
{
    if (g_body) return;   /* single-window demo */
    lv_obj_t *win = xkos_app_create(g_scr, title, 380, 280,
                                    de_app_close_cb, NULL);
    lv_obj_t *ct = xkos_app_content(win);

    xkos_text(ct, "XKOS app demo", XKOS_LABEL, 15, xkos_px(12), xkos_px(10));
    xkos_text(ct, "Live controls (D-Bus wired):", XKOS_LABEL_2, 12,
              xkos_px(12), xkos_px(38));

    xkos_toggle_create(ct, xkos_px(12), xkos_px(64), 1,
                       XKOS_DBUS_CC, "DemoToggle");
    xkos_slider_create(ct, xkos_px(12), xkos_px(104), xkos_px(320),
                       70, 0, 100, XKOS_DBUS_CC, "DemoSlider");
    static const char *seg[] = { "One", "Two", "Three" };
    xkos_segmented_create(ct, xkos_px(12), xkos_px(140),
                          seg, 3, 0, XKOS_DBUS_CC);
}

/* ===================================================================== */
/*  D-Bus handlers                                                         */
/* ===================================================================== */

static void menu_handler(const char *sender, const char *path,
                         const char *member, const char *arg, void *ud)
{
    (void)sender; (void)path; (void)member; (void)ud;

    if (!strcmp(arg, "About This XKOS")) {
        static const char *btns[] = { "OK" };
        xkos_dialog_create(g_scr, "About This XKOS",
                           "XKOS 26.0.8 (OpenArc-1)\nmacOS-styled desktop "
                           "environment\nD-Bus driven widget kit.",
                           btns, 1, NULL, NULL);
    } else if (!strcmp(arg, "Restart")) {
        de_reboot();
    } else if (!strcmp(arg, "Shut Down")) {
        de_poweroff();
    } else if (!strcmp(arg, "Sleep")) {
        xkos_notification_create(g_scr, "XKOS", "Sleeping",
                                 "System is going to sleep.",
                                 XKOS_PURPLE, "", 3000);
        klog("xkos.de", "sleep (demo)");
    } else if (!strcmp(arg, "Lock Screen")) {
        de_lock();
    }
}

static void status_handler(const char *sender, const char *path,
                           const char *member, const char *arg, void *ud)
{
    (void)sender; (void)path; (void)member; (void)ud;
    if (!strcmp(member, "Open")) {
        klog("xkos.de", "status open: %s", arg);
        xkos_popover_show(g_cc);
    }
}

static void desktop_handler(const char *sender, const char *path,
                            const char *member, const char *arg, void *ud)
{
    (void)sender; (void)path; (void)ud;
    if (!strcmp(member, "Query")) {
        if (!lv_obj_has_flag(g_lock, LV_OBJ_FLAG_HIDDEN)) {
            de_unlock();                 /* password field unlocks */
            return;
        }
        xkos_notification_create(g_scr, "Spotlight", "Search", arg,
                                 XKOS_ACCENT, "", 2500);
    }
}

static void cc_handler(const char *sender, const char *path,
                       const char *member, const char *arg, void *ud)
{
    (void)sender; (void)path; (void)ud;
    klog("xkos.de.cc", "%s = %s", member, arg);
}

/* ===================================================================== */
/*  Dock                                                                  */
/* ===================================================================== */

static void launch_finder(void)  { de_open_app("Finder"); }
static void launch_settings(void) { de_open_app("System Settings"); }
static void launch_terminal(void) { de_open_app("Terminal"); }
static void launch_about(void) {
    static const char *btns[] = { "OK" };
    xkos_dialog_create(g_scr, "About This XKOS",
                       "XKOS 26.0.8 (OpenArc-1)\nmacOS-styled DE.",
                       btns, 1, NULL, NULL);
}
static void launch_restart(void) { de_reboot(); }

static void de_build_dock(void)
{
    static const struct xkos_dock_app apps[] = {
        { "Finder",   "F", { .full = 0x0A7BFF }, { .full = 0x06449A }, launch_finder },
        { "Settings", "S", { .full = 0x9A9AA2 }, { .full = 0x5A5A64 }, launch_settings },
        { "Terminal", ">_",{ .full = 0x1E1E24 }, { .full = 0x0A0A0E }, launch_terminal },
        { "About",    "i", { .full = 0x6E6E78 }, { .full = 0x3A3A42 }, launch_about },
        { "Restart",  "R", { .full = 0xFE9F0A }, { .full = 0xA85402 }, launch_restart },
    };
    xkos_dock_create(g_scr, apps, (int)(sizeof(apps) / sizeof(apps[0])));
}

/* ===================================================================== */
/*  Entry points                                                          */
/* ===================================================================== */

void xkos_de_init(void)
{
    xkos_dbus_init();
}

void xkos_de_run(void)
{
    lv_coord_t sw, sh;

    if (!framebuffer_ready())
        return;

    xkos_dbus_init();   /* idempotent; also called from xkos_de_init() */

    sw = (lv_coord_t)framebuffer_width();
    sh = (lv_coord_t)framebuffer_height();

    lv_port_init((u32)sw, (u32)sh);   /* idempotent */
    xkos_scale_init();

    klog("xkos.de", "starting desktop %ux%u", (u32)sw, (u32)sh);

    /* Hand the screen to the DE: drop the kernel-log console.  Its per-tick
     * screen-scroll (driven by the PIT IRQ) would otherwise shove our
     * widgets off the top of the display. */
    lv_console_stop();
    g_scr = lv_scr_act();
    lv_obj_clean(g_scr);
    lv_obj_scroll_to_y(g_scr, 0, LV_ANIM_OFF);
    de_wallpaper();
    xkos_topbar_create(g_scr, "XKOS");
    de_icons();
    de_build_cc();
    de_build_lock();
    de_build_dock();

    /* register D-Bus handlers */
    xkos_dbus_listen(XKOS_DBUS_MENU,    menu_handler, NULL);
    xkos_dbus_listen(XKOS_DBUS_STATUS,  status_handler, NULL);
    xkos_dbus_listen(XKOS_DBUS_DESKTOP, desktop_handler, NULL);
    xkos_dbus_listen(XKOS_DBUS_CC,      cc_handler, NULL);

    /* Surface pointer availability (PS/2 or USB HID mouse) to the user. */
    if (input_mouse_present())
        xkos_notification_create(g_scr, "XKOS", "Pointer ready",
                                 "USB/PS2 mouse connected.",
                                 XKOS_ACCENT, "", 3000);

    /* run loop */
    u32 start = (u32)tsc_ms();
    u32 frame = 0;
    while (!input_pressed(GKEY_ESC)) {
        u32 now = (u32)tsc_ms();
        u32 dt  = (frame == 0) ? 16 : (now - start - (frame - 1) * 16);
        if (dt > 100) dt = 16;
        frame++;

        lv_port_poll();
        lv_timer_handler();

        {
            u32 target = frame * 16;
            s32 delay = (s32)target - (s32)((u32)tsc_ms() - start);
            if (delay > 0) {
                u64 end = tsc_ms() + (u64)delay;
                while (tsc_ms() < end)
                    asm volatile ("pause");
            }
        }
    }

    lv_obj_clean(g_scr);
    klog("xkos.de", "desktop exited");
}
