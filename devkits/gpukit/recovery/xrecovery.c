#include "xrecovery.h"
#include "xenv.h"
#include "xinstall.h"
#include "lv_port.h"
#include "lvgl.h"
#include "input.h"
#include "tsc.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include "devfs/devfs.h"
#include "IOGraphicsFamily/fb.h"

/* ===================================================================== */
/*  State                                                                  */
/* ===================================================================== */

enum util_action {
    UA_INSTALL = 0,
    UA_DISKUTIL,
    UA_TEXT,
    UA_DESKTOP,
    UA_RESTART,
    UA_SHUTDOWN,
};

struct util_item {
    const char *label;
    const char *desc;
    const char *glyph;
    lv_color_t top;
    lv_color_t bottom;
    int action;
};

#define RECOV_ITEMS 3

static const struct util_item g_items[RECOV_ITEMS] = {
    { "Install XKERN",
      "Install or reinstall the XKERN operating system.",
      "I", { .full = 0x0A7BFF }, { .full = 0x06449A },
      UA_INSTALL },
    { "Disk Utility",
      "Inspect, verify and erase attached disks.",
      "D", { .full = 0xBBBBC2 }, { .full = 0x6E6E78 },
      UA_DISKUTIL },
    { "Terminal",
      "Open the recovery command-line console.",
      ">_", { .full = 0x2E2E36 }, { .full = 0x121218 },
      UA_TEXT },
};

static const struct {
    const char *label;
    int action;
} g_menu_items[] = {
    { "About This Hardware", 100 },
    { "Disk Utility",        UA_DISKUTIL },
    { "Install XKERN",      UA_INSTALL },
    { "Boot XKERN",         UA_DESKTOP },
    { "Restart...",          UA_RESTART },
    { "Shut Down...",        UA_SHUTDOWN },
};
#define MENU_COUNT (sizeof(g_menu_items) / sizeof(g_menu_items[0]))

enum { MODE_MAIN = 0, MODE_DISKUTIL = 1 };

static int g_mode;
static int g_sel;
static int g_hover;    /* -1 = none, 0..RECOV_ITEMS-1 = mouse hover */
static int g_done;
static int g_return;
static int g_util_sel;
static int g_du_hover; /* disk-utility row hover, -1 = none */
static char g_util_status[96];

/* LVGL widget tree */
static lv_obj_t *g_scr;
static lv_obj_t *g_panel;
static lv_obj_t *g_util_panel;
static lv_obj_t *g_items_obj[RECOV_ITEMS];
static lv_obj_t *g_item_labels[RECOV_ITEMS];
static lv_obj_t *g_item_descs[RECOV_ITEMS];
static lv_obj_t *g_item_icons[RECOV_ITEMS];
static lv_obj_t *g_item_chevrons[RECOV_ITEMS];
static lv_obj_t *g_menubar;
static lv_obj_t *g_dropdown;
static lv_obj_t *g_status_label;

/* ===================================================================== */
/*  Helpers                                                                */
/* ===================================================================== */

static void set_selected(int idx);

/* ===================================================================== */
/*  Menubar callbacks                                                      */
/* ===================================================================== */

static void dropdown_click_cb(lv_event_t *e)
{
    lv_obj_t *item = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < (int)MENU_COUNT) {
        int action = g_menu_items[idx].action;
        /* close dropdown */
        if (g_dropdown)
            lv_obj_add_flag(g_dropdown, LV_OBJ_FLAG_HIDDEN);
        switch (action) {
        case 100: /* About */
            /* simple notification */
            break;
        case UA_DISKUTIL:
            g_mode = MODE_DISKUTIL;
            g_util_sel = 0;
            g_util_status[0] = '\0';
            lv_obj_add_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_util_panel, LV_OBJ_FLAG_HIDDEN);
            break;
        case UA_INSTALL:
            if (xenv_payload_present())
                xinstall_run();
            break;
        case UA_DESKTOP:
            g_done = 1;
            g_return = XRECOVERY_DESKTOP;
            break;
        case UA_TEXT:
            g_done = 1;
            g_return = XRECOVERY_TEXT;
            break;
        case UA_RESTART:
            xenv_reboot();
            break;
        case UA_SHUTDOWN:
            xenv_poweroff();
            break;
        }
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

/* ===================================================================== */
/*  Item click callbacks                                                   */
/* ===================================================================== */

static void item_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < RECOV_ITEMS) {
        set_selected(idx);
        switch (g_items[idx].action) {
        case UA_INSTALL:
            if (xenv_payload_present())
                xinstall_run();
            break;
        case UA_DISKUTIL:
            g_mode = MODE_DISKUTIL;
            g_util_sel = 0;
            g_util_status[0] = '\0';
            lv_obj_add_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_util_panel, LV_OBJ_FLAG_HIDDEN);
            break;
        case UA_TEXT:
            g_done = 1;
            g_return = XRECOVERY_TEXT;
            break;
        }
    }
}

/* ===================================================================== */
/*  Panel building                                                         */
/* ===================================================================== */

static void build_main_panel(void)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t sh = lv_disp_get_ver_res(NULL);
    lv_coord_t pw = 480;
    lv_coord_t ph = 380;
    lv_coord_t px_ = (sw - pw) / 2;
    lv_coord_t py_ = 28 + (sh - 28 - ph) / 2;

    g_panel = lv_obj_create(g_scr);
    lv_obj_set_size(g_panel, pw, ph);
    lv_obj_set_pos(g_panel, px_, py_);
    lv_obj_set_style_bg_color(g_panel, lv_color_hex(0x2E2E35), 0);
    lv_obj_set_style_bg_opa(g_panel, 240, 0);
    lv_obj_set_style_bg_grad_dir(g_panel, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(g_panel, 0, 0);
    lv_obj_set_style_bg_grad_stop(g_panel, 255, 0);
    lv_obj_set_style_bg_color(g_panel, lv_color_hex(0x202026),
                              LV_PART_MAIN | LV_STATE_USER_1);
    lv_obj_set_style_radius(g_panel, 14, 0);
    lv_obj_set_style_border_width(g_panel, 1, 0);
    lv_obj_set_style_border_color(g_panel, lv_color_hex(0x4A4A54), 0);
    lv_obj_set_style_border_opa(g_panel, 160, 0);
    lv_obj_set_style_shadow_width(g_panel, 20, 0);
    lv_obj_set_style_shadow_color(g_panel, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(g_panel, LV_OPA_50, 0);
    lv_obj_set_scrollbar_mode(g_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(g_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* title */
    lv_obj_t *title = lv_label_create(g_panel);
    lv_label_set_text(title, "XKERN Recovery");
    lv_obj_set_style_text_color(title, XENV_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

    /* subtitle */
    lv_obj_t *sub = lv_label_create(g_panel);
    lv_label_set_text(sub, "Choose an option to install, repair or access XKERN.");
    lv_obj_set_style_text_color(sub, XENV_GRAY, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 60);

    /* items */
    int i;
    lv_coord_t item_y = 106;
    lv_coord_t item_h = 66;
    lv_coord_t item_gap = 12;
    lv_coord_t item_w = pw - 48;

    for (i = 0; i < RECOV_ITEMS; i++) {
        lv_obj_t *item = lv_obj_create(g_panel);
        lv_obj_set_size(item, item_w, item_h);
        lv_obj_set_pos(item, 24, item_y + i * (item_h + item_gap));
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 10, 0);
        lv_obj_set_scrollbar_mode(item, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(item, (void *)(intptr_t)i);
        lv_obj_add_event_cb(item, item_click_cb, LV_EVENT_CLICKED, NULL);
        g_items_obj[i] = item;

        /* icon tile */
        lv_obj_t *icon = xenv_icon_tile_create(item, 0, (item_h - 42) / 2,
                                                42, 42,
                                                g_items[i].glyph, NULL,
                                                g_items[i].top,
                                                g_items[i].bottom);
        g_item_icons[i] = icon;

        /* label */
        lv_obj_t *lbl = lv_label_create(item);
        lv_label_set_text(lbl, g_items[i].label);
        lv_obj_set_style_text_color(lbl, XENV_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(lbl, 56, 12);
        g_item_labels[i] = lbl;

        /* description */
        lv_obj_t *desc = lv_label_create(item);
        lv_label_set_text(desc, g_items[i].desc);
        lv_obj_set_style_text_color(desc, XENV_GRAY, 0);
        lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(desc, 56, 36);
        g_item_descs[i] = desc;

        /* chevron */
        lv_obj_t *chev = lv_label_create(item);
        lv_label_set_text(chev, ">");
        lv_obj_set_style_text_color(chev, XENV_GRAY, 0);
        lv_obj_set_style_text_font(chev, &lv_font_montserrat_18, 0);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -16, 0);
        g_item_chevrons[i] = chev;
    }
}

/* ===================================================================== */
/*  Disk utility panel                                                     */
/* ===================================================================== */

static lv_obj_t *g_du_title;
static lv_obj_t *g_du_sub;
static lv_obj_t *g_du_rows[16];
static lv_obj_t *g_du_status;
static lv_obj_t *g_du_verify_btn;
static lv_obj_t *g_du_erase_btn;
static lv_obj_t *g_du_back_btn;
static int g_du_row_count;

static void du_verify_cb(lv_event_t *e)
{
    (void)e;
    const char *name;
    u8 buf[1024];

    if (xenv_disk_count() == 0) {
        snprintf(g_util_status, sizeof(g_util_status), "No disk selected.");
        lv_label_set_text(g_du_status, g_util_status);
        return;
    }
    if (g_util_sel >= xenv_disk_count())
        g_util_sel = 0;
    xenv_disk_get(g_util_sel, &name, 0, 0);
    if (devfs_read(name, 0, 2, buf) == 0)
        snprintf(g_util_status, sizeof(g_util_status),
                 "%s: 2 sectors verified, no errors.", name);
    else
        snprintf(g_util_status, sizeof(g_util_status),
                 "%s: verification failed.", name);
    lv_label_set_text(g_du_status, g_util_status);
}

static void du_erase_cb(lv_event_t *e)
{
    (void)e;
    const char *name;
    u8 zero[512];

    if (xenv_disk_count() == 0 || g_util_sel >= xenv_disk_count())
        return;
    xenv_disk_get(g_util_sel, &name, 0, 0);
    memset(zero, 0, sizeof(zero));
    if (devfs_write(name, 0, 1, zero) == 0)
        snprintf(g_util_status, sizeof(g_util_status),
                 "%s: erased (first sector zeroed).", name);
    else
        snprintf(g_util_status, sizeof(g_util_status),
                 "%s: erase failed.", name);
    lv_label_set_text(g_du_status, g_util_status);
}

static void du_back_cb(lv_event_t *e)
{
    (void)e;
    g_mode = MODE_MAIN;
    lv_obj_add_flag(g_util_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
}

static void du_row_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_util_sel = idx;
    /* visual update is handled in the tick */
}

static void build_diskutil_panel(void)
{
    lv_coord_t sw = lv_disp_get_hor_res(NULL);
    lv_coord_t sh = lv_disp_get_ver_res(NULL);
    lv_coord_t pw = 560;
    lv_coord_t ph = 420;
    lv_coord_t px_ = (sw - pw) / 2;
    lv_coord_t py_ = 28 + (sh - 28 - ph) / 2;
    int i, n;

    g_util_panel = lv_obj_create(g_scr);
    lv_obj_set_size(g_util_panel, pw, ph);
    lv_obj_set_pos(g_util_panel, px_, py_);
    lv_obj_set_style_bg_color(g_util_panel, lv_color_hex(0x2E2E35), 0);
    lv_obj_set_style_bg_opa(g_util_panel, 240, 0);
    lv_obj_set_style_radius(g_util_panel, 14, 0);
    lv_obj_set_style_border_width(g_util_panel, 1, 0);
    lv_obj_set_style_border_color(g_util_panel, lv_color_hex(0x4A4A54), 0);
    lv_obj_set_style_border_opa(g_util_panel, 160, 0);
    lv_obj_set_style_shadow_width(g_util_panel, 20, 0);
    lv_obj_set_style_shadow_color(g_util_panel, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(g_util_panel, LV_OPA_50, 0);
    lv_obj_set_scrollbar_mode(g_util_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(g_util_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_util_panel, LV_OBJ_FLAG_HIDDEN);

    /* title */
    g_du_title = lv_label_create(g_util_panel);
    lv_label_set_text(g_du_title, "Disk Utility");
    lv_obj_set_style_text_color(g_du_title, XENV_TEXT, 0);
    lv_obj_set_style_text_font(g_du_title, &lv_font_montserrat_26, 0);
    lv_obj_set_pos(g_du_title, 20, 18);

    /* subtitle */
    g_du_sub = lv_label_create(g_util_panel);
    lv_label_set_text(g_du_sub, "Select a disk, then verify or erase it.");
    lv_obj_set_style_text_color(g_du_sub, XENV_GRAY, 0);
    lv_obj_set_style_text_font(g_du_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(g_du_sub, 20, 52);

    /* disk rows */
    n = xenv_disk_count();
    g_du_row_count = n;
    lv_coord_t ry = 76;
    for (i = 0; i < n && i < 16; i++) {
        const char *name, *model;
        u32 mb;
        char line[96];

        if (xenv_disk_get(i, &name, &model, &mb) != 0) break;

        lv_obj_t *row = lv_obj_create(g_util_panel);
        lv_obj_set_size(row, pw - 40, 44);
        lv_obj_set_pos(row, 20, ry);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, du_row_click_cb, LV_EVENT_CLICKED, NULL);
        g_du_rows[i] = row;

        snprintf(line, sizeof(line), "/dev/%s  %u MB", name, mb);
        lv_obj_t *nmlbl = lv_label_create(row);
        lv_label_set_text(nmlbl, line);
        lv_obj_set_style_text_color(nmlbl, XENV_TEXT, 0);
        lv_obj_set_style_text_font(nmlbl, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(nmlbl, 14, 6);

        lv_obj_t *mdlbl = lv_label_create(row);
        lv_label_set_text(mdlbl, model);
        lv_obj_set_style_text_color(mdlbl, XENV_GRAY, 0);
        lv_obj_set_style_text_font(mdlbl, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(mdlbl, 14, 24);

        ry += 50;
    }
    if (n == 0) {
        lv_obj_t *empty = lv_label_create(g_util_panel);
        lv_label_set_text(empty, "No disks found.");
        lv_obj_set_style_text_color(empty, XENV_GRAY, 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(empty, 34, ry);
    }

    /* status */
    g_du_status = lv_label_create(g_util_panel);
    lv_label_set_text(g_du_status, "");
    lv_obj_set_style_text_color(g_du_status, XENV_GRAY, 0);
    lv_obj_set_style_text_font(g_du_status, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(g_du_status, 20, ph - 66);

    /* buttons */
    lv_coord_t bw = 120;
    lv_coord_t bh = 30;
    lv_coord_t by_ = ph - 46;
    lv_coord_t bx_ = pw - 3 * bw - 3 * 14;

    g_du_verify_btn = lv_btn_create(g_util_panel);
    lv_obj_set_size(g_du_verify_btn, bw, bh);
    lv_obj_set_pos(g_du_verify_btn, bx_, by_);
    lv_obj_set_style_bg_color(g_du_verify_btn, lv_color_hex(0x3F3F48), 0);
    lv_obj_set_style_radius(g_du_verify_btn, 6, 0);
    lv_obj_set_style_border_color(g_du_verify_btn, lv_color_hex(0x555560), 0);
    lv_obj_set_style_border_width(g_du_verify_btn, 1, 0);
    {
        lv_obj_t *lbl = lv_label_create(g_du_verify_btn);
        lv_label_set_text(lbl, "Verify");
        lv_obj_set_style_text_color(lbl, XENV_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(g_du_verify_btn, du_verify_cb, LV_EVENT_CLICKED, NULL);

    bx_ += bw + 14;
    g_du_erase_btn = lv_btn_create(g_util_panel);
    lv_obj_set_size(g_du_erase_btn, bw, bh);
    lv_obj_set_pos(g_du_erase_btn, bx_, by_);
    lv_obj_set_style_bg_color(g_du_erase_btn, lv_color_hex(0x3F3F48), 0);
    lv_obj_set_style_radius(g_du_erase_btn, 6, 0);
    lv_obj_set_style_border_color(g_du_erase_btn, lv_color_hex(0x555560), 0);
    lv_obj_set_style_border_width(g_du_erase_btn, 1, 0);
    {
        lv_obj_t *lbl = lv_label_create(g_du_erase_btn);
        lv_label_set_text(lbl, "Erase...");
        lv_obj_set_style_text_color(lbl, XENV_RED, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(g_du_erase_btn, du_erase_cb, LV_EVENT_CLICKED, NULL);

    bx_ += bw + 14;
    g_du_back_btn = lv_btn_create(g_util_panel);
    lv_obj_set_size(g_du_back_btn, bw, bh);
    lv_obj_set_pos(g_du_back_btn, bx_, by_);
    lv_obj_set_style_bg_color(g_du_back_btn, lv_color_hex(0x3F3F48), 0);
    lv_obj_set_style_radius(g_du_back_btn, 6, 0);
    lv_obj_set_style_border_color(g_du_back_btn, lv_color_hex(0x555560), 0);
    lv_obj_set_style_border_width(g_du_back_btn, 1, 0);
    {
        lv_obj_t *lbl = lv_label_create(g_du_back_btn);
        lv_label_set_text(lbl, "Back");
        lv_obj_set_style_text_color(lbl, XENV_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(g_du_back_btn, du_back_cb, LV_EVENT_CLICKED, NULL);
}

/* ===================================================================== */
/*  Selection highlight update                                             */
/* ===================================================================== */

static void set_selected(int idx)
{
    int i;
    g_sel = idx;
    for (i = 0; i < RECOV_ITEMS; i++) {
        if (i == idx) {
            lv_obj_set_style_bg_color(g_items_obj[i], XENV_SEL_BG, 0);
            lv_obj_set_style_bg_opa(g_items_obj[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(g_items_obj[i], 1, 0);
            lv_obj_set_style_border_color(g_items_obj[i], XENV_SEL_BORDER, 0);
            lv_obj_set_style_text_color(g_item_chevrons[i], XENV_BLUE, 0);
        } else {
            lv_obj_set_style_bg_opa(g_items_obj[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(g_items_obj[i], 0, 0);
            lv_obj_set_style_text_color(g_item_chevrons[i], XENV_GRAY, 0);
        }
    }
}

static void update_du_selection(void)
{
    int i;
    for (i = 0; i < g_du_row_count && i < 16; i++) {
        if (i == g_util_sel) {
            lv_obj_set_style_bg_color(g_du_rows[i], XENV_SEL_BG, 0);
            lv_obj_set_style_bg_opa(g_du_rows[i], 235, 0);
        } else {
            lv_obj_set_style_bg_opa(g_du_rows[i], LV_OPA_TRANSP, 0);
        }
    }
}

/* ===================================================================== */
/*  Mouse hover tracking                                                  */
/* ===================================================================== */

/* Test whether a screen-space point lies inside an LVGL object's coords. */
static int point_in_obj(lv_point_t *pt, lv_obj_t *obj)
{
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return pt->x >= a.x1 && pt->x <= a.x2 &&
           pt->y >= a.y1 && pt->y <= a.y2;
}

/*
 * Query the pointer indev for the current mouse position and update the
 * hover index.  Called once per frame before keyboard input.
 */
static void update_hover(void)
{
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t mp;
    int i;

    g_hover     = -1;
    g_du_hover  = -1;

    if (!indev) return;
    lv_indev_get_point(indev, &mp);

    if (g_mode == MODE_MAIN) {
        for (i = 0; i < RECOV_ITEMS; i++) {
            if (point_in_obj(&mp, g_items_obj[i])) {
                g_hover = i;
                break;
            }
        }
    } else {
        for (i = 0; i < g_du_row_count && i < 16; i++) {
            if (point_in_obj(&mp, g_du_rows[i])) {
                g_du_hover = i;
                break;
            }
        }
    }
}

/*
 * Apply hover highlight: a subtle tint on the hovered item when it
 * is not the currently selected item (keyboard selection takes priority).
 */
static void apply_hover(void)
{
    int i;
    if (g_mode == MODE_MAIN) {
        for (i = 0; i < RECOV_ITEMS; i++) {
            if (i == g_sel) {
                /* keyboard selection already styled by set_selected */
                continue;
            }
            if (i == g_hover) {
                lv_obj_set_style_bg_opa(g_items_obj[i], LV_OPA_10, 0);
            } else {
                lv_obj_set_style_bg_opa(g_items_obj[i], LV_OPA_TRANSP, 0);
            }
        }
    } else {
        for (i = 0; i < g_du_row_count && i < 16; i++) {
            if (i == g_util_sel) continue;
            if (i == g_du_hover) {
                lv_obj_set_style_bg_opa(g_du_rows[i], LV_OPA_10, 0);
            } else {
                lv_obj_set_style_bg_opa(g_du_rows[i], LV_OPA_TRANSP, 0);
            }
        }
    }
}

/*
 * Left-click on a hovered item: activate it the same way Enter would.
 * Called from the run loop when we see a left-button press edge.
 */
static void handle_mouse_click(void)
{
    int idx;
    if (g_mode == MODE_MAIN) {
        idx = g_hover;
        if (idx < 0) return;
        set_selected(idx);
        switch (g_items[idx].action) {
        case UA_INSTALL:
            if (xenv_payload_present())
                xinstall_run();
            break;
        case UA_DISKUTIL:
            g_mode = MODE_DISKUTIL;
            g_util_sel = 0;
            g_util_status[0] = '\0';
            lv_obj_add_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_util_panel, LV_OBJ_FLAG_HIDDEN);
            break;
        case UA_TEXT:
            g_done = 1;
            g_return = XRECOVERY_TEXT;
            break;
        }
    } else {
        idx = g_du_hover;
        if (idx < 0) return;
        g_util_sel = idx;
        update_du_selection();
    }
}

/* ===================================================================== */
/*  Entry point                                                            */
/* ===================================================================== */

int xrecovery_run(void)
{
    lv_coord_t sw, sh;
    u32 start;
    u32 frame = 0;
    u8  prev_mbtn = 0;

    if (!framebuffer_ready())
        return XRECOVERY_TEXT;

    sw = (lv_coord_t)framebuffer_width();
    sh = (lv_coord_t)framebuffer_height();

    /* init LVGL port (idempotent) */
    lv_port_init((u32)sw, (u32)sh);

    /* reset state */
    g_mode = MODE_MAIN;
    g_sel = 0;
    g_hover = -1;
    g_done = 0;
    g_return = XRECOVERY_DESKTOP;
    g_util_sel = 0;
    g_du_hover = -1;
    g_util_status[0] = '\0';

    /* build widget tree */
    g_scr = lv_scr_act();
    lv_obj_set_style_bg_color(g_scr, XENV_BG, 0);
    lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);

    xenv_wallpaper_create(g_scr);
    g_menubar = xenv_menubar_create(g_scr, "XKERN Recovery",
                                    NULL, 0);

    /* wire menubar click to toggle dropdown */
    lv_obj_clear_flag(g_menubar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_menubar, LV_OBJ_FLAG_CLICKABLE);
    /* replace the generic click handler with our dropdown toggle */
    lv_obj_remove_event_cb(g_menubar, NULL);
    lv_obj_add_event_cb(g_menubar, menubar_click_cb, LV_EVENT_CLICKED, NULL);

    /* build dropdown */
    {
        const char *names[MENU_COUNT];
        int i;
        for (i = 0; i < (int)MENU_COUNT; i++)
            names[i] = g_menu_items[i].label;
        g_dropdown = xenv_menubar_dropdown_create(g_scr, names,
                                                  MENU_COUNT, 32);
        /* wire individual dropdown items */
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

    build_main_panel();
    build_diskutil_panel();
    set_selected(0);

    /* --- run loop --------------------------------------------------------- */
    klog("xrecovery", "entering LVGL run loop");
    start = (u32)tsc_ms();

    while (!g_done) {
        u32 now = (u32)tsc_ms();
        u32 dt = (frame == 0) ? 16 : (now - start - (frame - 1) * 16);
        if (dt > 100) dt = 16;
        frame++;

        lv_port_poll();
        lv_timer_handler();

        /* frame pacing: ~60 fps */
        {
            u32 target = frame * 16;
            s32 delay = (s32)target - (s32)((u32)tsc_ms() - start);
            if (delay > 0) {
                u64 end = tsc_ms() + (u64)delay;
                while (tsc_ms() < end)
                    asm volatile ("pause");
            }
        }

        /* keyboard input */
        if (g_mode == MODE_DISKUTIL) {
            if (input_pressed(GKEY_ESC)) {
                g_mode = MODE_MAIN;
                lv_obj_add_flag(g_util_panel, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
            }
            if (input_pressed(GKEY_UP) && g_util_sel > 0)
                g_util_sel--;
            if (input_pressed(GKEY_DOWN) && g_util_sel < g_du_row_count - 1)
                g_util_sel++;
            if (input_pressed(GKEY_ENTER)) {
                /* trigger verify */
                lv_event_send(g_du_verify_btn, LV_EVENT_CLICKED, NULL);
            }
            update_du_selection();
        } else {
            if (input_pressed(GKEY_UP) && g_sel > 0)
                set_selected(g_sel - 1);
            if (input_pressed(GKEY_DOWN) && g_sel < RECOV_ITEMS - 1)
                set_selected(g_sel + 1);
            if (input_pressed(GKEY_ENTER)) {
                switch (g_items[g_sel].action) {
                case UA_INSTALL:
                    if (xenv_payload_present())
                        xinstall_run();
                    break;
                case UA_DISKUTIL:
                    g_mode = MODE_DISKUTIL;
                    g_util_sel = 0;
                    g_util_status[0] = '\0';
                    lv_obj_add_flag(g_panel, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(g_util_panel, LV_OBJ_FLAG_HIDDEN);
                    break;
                case UA_TEXT:
                    g_done = 1;
                    g_return = XRECOVERY_TEXT;
                    break;
                }
            }
            if (input_pressed(GKEY_ESC)) {
                g_done = 1;
                g_return = XRECOVERY_DESKTOP;
            }
        }

        /* mouse hover + click */
        update_hover();
        apply_hover();
        {
            struct input_mouse m;
            u8 cur_mbtn = 0;
            if (input_mouse(&m))
                cur_mbtn = m.buttons & 1;
            if (cur_mbtn && !prev_mbtn)
                handle_mouse_click();
            prev_mbtn = cur_mbtn;
        }
    }

    /* clean up */
    lv_obj_clean(g_scr);
    input_clear();
    klog("xrecovery", "exiting LVGL run loop");

    return g_return;
}
