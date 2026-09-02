#include "xdesktop.h"
#include "xenv.h"
#include "lv_port.h"
#include "lvgl.h"
#include "input.h"
#include "tsc.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include "init_ram_getty.h"
#include "doom.h"
#include "IOGraphicsFamily/fb.h"

/* ===================================================================== */
/*  Constants                                                              */
/* ===================================================================== */

#define MENUBAR_H  28
#define DOCK_BASE  54
#define DOCK_BUMP  34
#define DOCK_GAP   14
#define DOCK_ICON_N 6

/* ===================================================================== */
/*  Dock app descriptors                                                   */
/* ===================================================================== */

struct dock_app {
    const char *label;
    const char *glyph;
    lv_color_t top;
    lv_color_t bottom;
    void (*launch)(void);
    int running;
    lv_obj_t *obj;
    lv_obj_t *label_obj;
};

static struct dock_app g_apps[DOCK_ICON_N] = {
    { "Finder", "F",
      { .full = 0x0A7BFF }, { .full = 0x06449A }, NULL, 0, NULL, NULL },
    { "Settings", "S",
      { .full = 0x9A9AA2 }, { .full = 0x5A5A64 }, NULL, 0, NULL, NULL },
    { "About", "i",
      { .full = 0x6E6E78 }, { .full = 0x3A3A42 }, NULL, 0, NULL, NULL },
    { "Terminal", ">_",
      { .full = 0x1E1E24 }, { .full = 0x0A0A0E }, NULL, 0, NULL, NULL },
    { "DOOM", "D",
      { .full = 0xC83A30 }, { .full = 0x5C1410 }, NULL, 0, NULL, NULL },
    { "Restart", "R",
      { .full = 0xFE9F0A }, { .full = 0xA85402 }, NULL, 0, NULL, NULL },
};

static void launch_finder(void);
static void launch_settings(void);
static void launch_about(void);
static void launch_terminal(void);
static void launch_doom(void);
static void launch_restart(void);

static void (*launchers[])(void) = {
    launch_finder, launch_settings, launch_about,
    launch_terminal, launch_doom, launch_restart,
};

/* ===================================================================== */
/*  Global state                                                           */
/* ===================================================================== */

static lv_obj_t *g_scr;
static lv_obj_t *g_dock_bar;
static lv_obj_t *g_menubar;
static lv_obj_t *g_dropdown;

/* window containers (NULL = not open) */
static lv_obj_t *g_win_finder;
static lv_obj_t *g_win_settings;
static lv_obj_t *g_win_about;
static lv_obj_t *g_win_terminal;

/* ===================================================================== */
/*  Menu actions                                                           */
/* ===================================================================== */

enum { MENU_ABOUT = 100, MENU_DOCK_FINDER, MENU_DOCK_SETTINGS,
       MENU_DOCK_ABOUT, MENU_DOCK_TERMINAL, MENU_DOCK_DOOM, MENU_DOCK_RESTART };

static const struct {
    const char *label;
    int action;
} g_menu_items[] = {
    { "About This Mac",  MENU_ABOUT },
    { "Finder",          MENU_DOCK_FINDER },
    { "System Settings", MENU_DOCK_SETTINGS },
    { "Terminal",        MENU_DOCK_TERMINAL },
    { "DOOM Raycaster",  MENU_DOCK_DOOM },
    { "Restart...",      MENU_DOCK_RESTART },
};
#define MENU_COUNT (sizeof(g_menu_items) / sizeof(g_menu_items[0]))

/* ===================================================================== */
/*  Forward                                                                */
/* ===================================================================== */

static void close_window(lv_obj_t **win, int app_idx);

/* ===================================================================== */
/*  Window close button                                                    */
/* ===================================================================== */

static void win_close_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *win = lv_obj_get_parent(btn);
    /* find which window this is */
    if (win == g_win_finder)   { close_window(&g_win_finder, 0); return; }
    if (win == g_win_settings) { close_window(&g_win_settings, 1); return; }
    if (win == g_win_about)    { close_window(&g_win_about, 2); return; }
    if (win == g_win_terminal) { close_window(&g_win_terminal, 3); return; }
}

static void close_window(lv_obj_t **win, int app_idx)
{
    if (*win) {
        lv_obj_del(*win);
        *win = NULL;
        g_apps[app_idx].running = 0;
        g_apps[app_idx].obj = NULL;
    }
}

/* ===================================================================== */
/*  Create a basic window                                                  */
/* ===================================================================== */

static lv_obj_t *create_window(lv_coord_t x, lv_coord_t y,
                               lv_coord_t w, lv_coord_t h,
                               const char *title)
{
    lv_obj_t *win = lv_obj_create(g_scr);
    lv_obj_set_size(win, w, h);
    lv_obj_set_pos(win, x, y);
    lv_obj_set_style_bg_color(win, lv_color_hex(0x222226), 0);
    lv_obj_set_style_radius(win, 10, 0);
    lv_obj_set_style_border_color(win, lv_color_hex(0x3E3E44), 0);
    lv_obj_set_style_border_width(win, 1, 0);
    lv_obj_set_style_shadow_width(win, 16, 0);
    lv_obj_set_style_shadow_color(win, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(win, LV_OPA_40, 0);
    lv_obj_set_scrollbar_mode(win, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(win, LV_OBJ_FLAG_CLICKABLE);

    /* title bar */
    lv_obj_t *bar = lv_obj_create(win);
    lv_obj_set_size(bar, w, 28);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_radius(bar, 10, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tlbl = lv_label_create(bar);
    lv_label_set_text(tlbl, title);
    lv_obj_set_style_text_color(tlbl, XENV_TEXT, 0);
    lv_obj_set_style_text_font(tlbl, &lv_font_montserrat_12, 0);
    lv_obj_align(tlbl, LV_ALIGN_LEFT_MID, 10, 0);

    /* close button */
    lv_obj_t *cbtn = lv_btn_create(bar);
    lv_obj_set_size(cbtn, 12, 12);
    lv_obj_align(cbtn, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_color(cbtn, XENV_RED, 0);
    lv_obj_set_style_radius(cbtn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cbtn, 0, 0);
    lv_obj_add_event_cb(cbtn, win_close_cb, LV_EVENT_CLICKED, NULL);

    return win;
}

static lv_obj_t *win_label(lv_obj_t *win, lv_coord_t x, lv_coord_t y,
                           const char *text, lv_color_t c,
                           const lv_font_t *font)
{
    lv_obj_t *lbl = lv_label_create(win);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, c, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_pos(lbl, x, y);
    return lbl;
}

/* ===================================================================== */
/*  App launchers                                                          */
/* ===================================================================== */

static void launch_finder(void)
{
    if (g_win_finder) { lv_obj_move_foreground(g_win_finder); return; }
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    g_win_finder = create_window(sw - 280, 60, 260, 300, "Finder");
    g_apps[0].obj = g_win_finder;
    g_apps[0].running = 1;

    win_label(g_win_finder, 8, 34, "XKERN (recovery initram)", XENV_TEXT,
              &lv_font_montserrat_14);
    win_label(g_win_finder, 8, 56, "System files:", XENV_GRAY,
              &lv_font_montserrat_12);

    struct ramfs *fs = ramfs_get();
    lv_coord_t fy = 78;
    int rows = 0;
    if (fs) {
        int i;
        for (i = 0; i < fs->count && rows < 10; i++) {
            struct ramfs_file *f = &fs->files[i];
            char line[96];
            snprintf(line, sizeof(line), "/%s  (%u B)", f->name, f->size);
            win_label(g_win_finder, 16, fy, line, XENV_BLUE,
                      &lv_font_montserrat_12);
            fy += 22;
            rows++;
        }
    }
    if (rows == 0)
        win_label(g_win_finder, 16, fy, "(empty)", XENV_GRAY,
                  &lv_font_montserrat_12);
}

static void on_dark_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    (void)cb;
    /* dark mode toggle - simplified */
}

static void on_volume_cb(lv_event_t *e)
{
    (void)e;
}

static void launch_settings(void)
{
    if (g_win_settings) { lv_obj_move_foreground(g_win_settings); return; }
    g_win_settings = create_window(440, 120, 340, 280, "System Settings");
    g_apps[1].obj = g_win_settings;
    g_apps[1].running = 1;

    win_label(g_win_settings, 8, 34, "Appearance", XENV_TEXT,
              &lv_font_montserrat_16);

    lv_obj_t *cb = lv_checkbox_create(g_win_settings);
    lv_checkbox_set_text(cb, "Dark mode");
    lv_obj_set_style_text_color(cb, XENV_TEXT, 0);
    lv_obj_set_style_text_font(cb, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(cb, 8, 60);
    lv_obj_add_event_cb(cb, on_dark_cb, LV_EVENT_CLICKED, NULL);

    win_label(g_win_settings, 8, 100, "Volume", XENV_TEXT,
              &lv_font_montserrat_16);

    lv_obj_t *sl = lv_slider_create(g_win_settings);
    lv_slider_set_range(sl, 0, 100);
    lv_slider_set_value(sl, 60, LV_ANIM_OFF);
    lv_obj_set_pos(sl, 8, 128);
    lv_obj_set_size(sl, 260, 14);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x1A1A1E), 0);
    lv_obj_set_style_radius(sl, 7, 0);
    lv_obj_set_style_bg_color(sl, XENV_BLUE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(sl, 7, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(sl, on_volume_cb, LV_EVENT_VALUE_CHANGED, NULL);

    win_label(g_win_settings, 8, 158, "UI scale", XENV_TEXT,
              &lv_font_montserrat_16);

    lv_obj_t *sl2 = lv_slider_create(g_win_settings);
    lv_slider_set_range(sl2, 50, 100);
    lv_slider_set_value(sl2, 70, LV_ANIM_OFF);
    lv_obj_set_pos(sl2, 8, 186);
    lv_obj_set_size(sl2, 260, 14);
    lv_obj_set_style_bg_color(sl2, lv_color_hex(0x1A1A1E), 0);
    lv_obj_set_style_radius(sl2, 7, 0);
    lv_obj_set_style_bg_color(sl2, XENV_BLUE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(sl2, 7, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    win_label(g_win_settings, 8, 224, "XKERN 0.1.0  (OpenArc-1)",
              XENV_GRAY, &lv_font_montserrat_12);
}

static void launch_about(void)
{
    if (g_win_about) { lv_obj_move_foreground(g_win_about); return; }
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    g_win_about = create_window(sw / 2 - 180, 200, 360, 230,
                                "About This Mac");
    g_apps[2].obj = g_win_about;
    g_apps[2].running = 1;

    win_label(g_win_about, 8, 34, "XKERN", XENV_TEXT,
              &lv_font_montserrat_24);
    win_label(g_win_about, 8, 66, "macOS-styled recovery & desktop",
              XENV_GRAY, &lv_font_montserrat_12);

    char line[96];
    snprintf(line, sizeof(line), "Chip:  i386 (x87)");
    win_label(g_win_about, 8, 92, line, XENV_TEXT, &lv_font_montserrat_12);
    snprintf(line, sizeof(line), "Memory:  PMM-managed");
    win_label(g_win_about, 8, 112, line, XENV_TEXT, &lv_font_montserrat_12);
    snprintf(line, sizeof(line), "Display:  %ux%u",
             lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    win_label(g_win_about, 8, 132, line, XENV_TEXT, &lv_font_montserrat_12);
    snprintf(line, sizeof(line), "Serial:   XKERN-0.1.0");
    win_label(g_win_about, 8, 152, line, XENV_TEXT, &lv_font_montserrat_12);
}

static void launch_terminal(void)
{
    if (g_win_terminal) { lv_obj_move_foreground(g_win_terminal); return; }
    g_win_terminal = create_window(500, 340, 420, 260, "Terminal");
    g_apps[3].obj = g_win_terminal;
    g_apps[3].running = 1;

    win_label(g_win_terminal, 8, 34, "XKERN console  (recovery shell)",
              XENV_WHITE, &lv_font_montserrat_14);
    win_label(g_win_terminal, 8, 56, "user@xkern:~$ version",
              XENV_GREEN, &lv_font_montserrat_12);
    win_label(g_win_terminal, 8, 78, "XKERN 0.1.0 (OpenArc-1) i386",
              XENV_WHITE, &lv_font_montserrat_12);
    win_label(g_win_terminal, 8, 100, "user@xkern:~$ pid",
              XENV_GREEN, &lv_font_montserrat_12);
    win_label(g_win_terminal, 8, 122, "pid 1", XENV_WHITE,
              &lv_font_montserrat_12);

    char line[96];
    u32 up = (u32)(tsc_ms() / 1000);
    snprintf(line, sizeof(line), "user@xkern:~$ uptime  %u:%02u",
             up / 60, up % 60);
    win_label(g_win_terminal, 8, 144, line, XENV_GREEN,
              &lv_font_montserrat_12);
    win_label(g_win_terminal, 8, 166, "The desktop is running in the",
              XENV_WHITE, &lv_font_montserrat_12);
    win_label(g_win_terminal, 8, 186, "kernel framebuffer. Press ESC to",
              XENV_WHITE, &lv_font_montserrat_12);
    win_label(g_win_terminal, 8, 206, "quit to the initram console.",
              XENV_WHITE, &lv_font_montserrat_12);
}

static void launch_doom(void)
{
    klog("xdesktop", "launching DOOM raycaster");
    input_clear();
    doom_run();
    input_clear();
}

static void launch_restart(void)
{
    xenv_reboot();
}

/* ===================================================================== */
/*  Dock                                                                  */
/* ===================================================================== */

static void dock_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < DOCK_ICON_N && launchers[idx])
        launchers[idx]();
}

static void build_dock(void)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t sh = lv_disp_get_ver_res(NULL);
    lv_coord_t n = DOCK_ICON_N;
    lv_coord_t total_w = n * DOCK_BASE + (n - 1) * DOCK_GAP;
    lv_coord_t bw = total_w + 28;
    lv_coord_t bh = 66;
    lv_coord_t bx = (sw - bw) / 2;
    lv_coord_t by = sh - 52;

    g_dock_bar = xenv_dock_bar_create(g_scr);

    lv_coord_t x0 = (sw - total_w) / 2;
    int i;
    for (i = 0; i < DOCK_ICON_N; i++) {
        lv_coord_t cx = x0 + i * (DOCK_BASE + DOCK_GAP) + DOCK_BASE / 2;
        lv_coord_t cy = by + 30;

        lv_obj_t *obj = lv_obj_create(g_scr);
        lv_obj_set_size(obj, DOCK_BASE, DOCK_BASE);
        lv_obj_set_pos(obj, cx - DOCK_BASE / 2, cy - DOCK_BASE / 2);
        lv_obj_set_style_bg_color(obj, g_apps[i].top, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(obj, g_apps[i].bottom,
                                  LV_PART_MAIN | LV_STATE_USER_1);
        lv_obj_set_style_radius(obj, DOCK_BASE / 5, 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_set_style_border_color(obj, lv_color_black(), 0);
        lv_obj_set_style_border_opa(obj, 50, 0);
        lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(obj, (void *)(intptr_t)i);
        lv_obj_add_event_cb(obj, dock_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        g_apps[i].obj = obj;

        /* glyph */
        lv_obj_t *glyph = lv_label_create(obj);
        lv_label_set_text(glyph, g_apps[i].glyph);
        lv_obj_set_style_text_color(glyph, lv_color_white(), 0);
        lv_obj_set_style_text_font(glyph, &lv_font_montserrat_20, 0);
        lv_obj_center(glyph);

        /* label below */
        lv_obj_t *lbl = lv_label_create(g_scr);
        lv_label_set_text(lbl, g_apps[i].label);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_opa(lbl, LV_OPA_0, 0);
        lv_obj_set_pos(lbl, cx - 20, cy + DOCK_BASE / 2 + 4);
        g_apps[i].label_obj = lbl;
    }
}

/* ===================================================================== */
/*  Desktop icons                                                          */
/* ===================================================================== */

static void desktop_icon_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    (void)idx;
}

static void build_desktop_icons(void)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t x = sw - 60;
    lv_coord_t y1 = 90;

    /* Hard Disk */
    lv_obj_t *hd = lv_obj_create(g_scr);
    lv_obj_set_size(hd, 64, 64);
    lv_obj_set_pos(hd, x - 32, y1 - 32);
    lv_obj_set_style_bg_color(hd, lv_color_hex(0x4A9AF0), 0);
    lv_obj_set_style_bg_opa(hd, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hd, 64 / 5, 0);
    lv_obj_set_style_border_width(hd, 0, 0);
    lv_obj_set_scrollbar_mode(hd, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(hd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(hd, (void *)0);
    lv_obj_add_event_cb(hd, desktop_icon_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *glyph = lv_label_create(hd);
    lv_label_set_text(glyph, "HD");
    lv_obj_set_style_text_color(glyph, lv_color_white(), 0);
    lv_obj_set_style_text_font(glyph, &lv_font_montserrat_16, 0);
    lv_obj_center(glyph);

    lv_obj_t *hd_lbl = lv_label_create(g_scr);
    lv_label_set_text(hd_lbl, "Hard Disk");
    lv_obj_set_style_text_color(hd_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(hd_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(hd_lbl, x - 30, y1 + 38);

    /* Trash */
    lv_obj_t *tr = lv_obj_create(g_scr);
    lv_obj_set_size(tr, 64, 64);
    lv_obj_set_pos(tr, x - 32, y1 + 120 - 32);
    lv_obj_set_style_bg_color(tr, lv_color_hex(0xC8C8D0), 0);
    lv_obj_set_style_bg_opa(tr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tr, 64 / 5, 0);
    lv_obj_set_style_border_width(tr, 0, 0);
    lv_obj_set_scrollbar_mode(tr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(tr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(tr, (void *)1);
    lv_obj_add_event_cb(tr, desktop_icon_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *glyph2 = lv_label_create(tr);
    lv_label_set_text(glyph2, "x");
    lv_obj_set_style_text_color(glyph2, lv_color_white(), 0);
    lv_obj_set_style_text_font(glyph2, &lv_font_montserrat_16, 0);
    lv_obj_center(glyph2);

    lv_obj_t *tr_lbl = lv_label_create(g_scr);
    lv_label_set_text(tr_lbl, "Trash");
    lv_obj_set_style_text_color(tr_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(tr_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(tr_lbl, x - 20, y1 + 120 + 38);
}

/* ===================================================================== */
/*  Menu bar                                                               */
/* ===================================================================== */

static void dropdown_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)MENU_COUNT) return;
    if (g_dropdown)
        lv_obj_add_flag(g_dropdown, LV_OBJ_FLAG_HIDDEN);

    switch (g_menu_items[idx].action) {
    case MENU_ABOUT:       launch_about();    break;
    case MENU_DOCK_FINDER: launch_finder();   break;
    case MENU_DOCK_SETTINGS: launch_settings(); break;
    case MENU_DOCK_TERMINAL: launch_terminal(); break;
    case MENU_DOCK_DOOM:   launch_doom();     break;
    case MENU_DOCK_RESTART: launch_restart();  break;
    }
}

static void menubar_click_cb(lv_event_t *e)
{
    lv_obj_t *bar = lv_event_get_target(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t mp;
    lv_indev_get_point(indev, &mp);
    lv_area_t area;
    lv_obj_get_coords(bar, &area);

    if (mp.x < area.x1 + 200) {
        if (g_dropdown && lv_obj_has_flag(g_dropdown, LV_OBJ_FLAG_HIDDEN))
            lv_obj_clear_flag(g_dropdown, LV_OBJ_FLAG_HIDDEN);
        else if (g_dropdown)
            lv_obj_add_flag(g_dropdown, LV_OBJ_FLAG_HIDDEN);
    } else if (g_dropdown) {
        lv_obj_add_flag(g_dropdown, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_menubar(void)
{
    const char *names[MENU_COUNT];
    int i;
    for (i = 0; i < (int)MENU_COUNT; i++)
        names[i] = g_menu_items[i].label;

    g_menubar = xenv_menubar_create(g_scr, "XKERN", names, MENU_COUNT);

    /* replace generic click with our handler */
    lv_obj_remove_event_cb(g_menubar, NULL);
    lv_obj_add_event_cb(g_menubar, menubar_click_cb, LV_EVENT_CLICKED, NULL);

    /* wire dropdown items */
    if (g_dropdown) {
        lv_obj_t *dd = g_dropdown;
        uint32_t ci;
        for (ci = 0; ci < lv_obj_get_child_cnt(dd); ci++) {
            lv_obj_t *child = lv_obj_get_child(dd, ci);
            if (ci < MENU_COUNT) {
                lv_obj_add_flag(child, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_user_data(child, (void *)(intptr_t)ci);
                lv_obj_add_event_cb(child, dropdown_click_cb,
                                    LV_EVENT_CLICKED, (void *)(intptr_t)ci);
            }
        }
    }
}

/* ===================================================================== */
/*  Entry point                                                            */
/* ===================================================================== */

void xdesktop_run(void)
{
    lv_coord_t sw, sh;
    u32 start;
    u32 frame = 0;

    if (!framebuffer_ready())
        return;

    sw = (lv_coord_t)framebuffer_width();
    sh = (lv_coord_t)framebuffer_height();

    /* init LVGL port (idempotent) */
    lv_port_init((u32)sw, (u32)sh);

    /* reset state */
    g_win_finder = g_win_settings = g_win_about = g_win_terminal = NULL;
    {
        int i;
        for (i = 0; i < DOCK_ICON_N; i++)
            g_apps[i].running = 0;
    }

    /* build widget tree */
    g_scr = lv_scr_act();
    lv_obj_set_style_bg_color(g_scr, XENV_BG, 0);
    lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);

    xenv_wallpaper_create(g_scr);
    build_menubar();
    build_desktop_icons();
    build_dock();

    /* auto-launch Finder */
    launch_finder();

    klog("xdesktop", "entering LVGL desktop run loop");
    start = (u32)tsc_ms();

    /* --- run loop --------------------------------------------------------- */
    while (!input_pressed(GKEY_ESC)) {
        u32 now = (u32)tsc_ms();
        u32 dt = (frame == 0) ? 16 : (now - start - (frame - 1) * 16);
        if (dt > 100) dt = 16;
        frame++;

        lv_port_poll();
        lv_timer_handler();

        /* frame pacing */
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

    /* clean up */
    lv_obj_clean(g_scr);
    input_clear();
    klog("xdesktop", "desktop run loop exited");
}
