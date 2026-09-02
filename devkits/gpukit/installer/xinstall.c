#include "xinstall.h"
#include "xenv.h"
#include "lv_port.h"
#include "lvgl.h"
#include "input.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include "devfs/devfs.h"
#include "tsc.h"


enum xpage {
    XP_WELCOME = 0,
    XP_DISK,
    XP_CONFIRM,
    XP_PROGRESS,
    XP_DONE,
};

#define XPANEL_W 560
#define XPANEL_H 400

static int g_page;
static int g_sel;
static int g_done;
static int g_result;
static u32 g_lba;
static u32 g_total_sectors;
static char g_disk_name[32];
static char g_disk_model[48];
static u32 g_disk_mb;

static lv_obj_t *g_scr;
static lv_obj_t *g_panel;
static lv_obj_t *g_pages[5]; /* one container per page */
static lv_obj_t *g_btn_back;
static lv_obj_t *g_btn_next;

/* progress widgets */
static lv_obj_t *g_prog_bar;
static lv_obj_t *g_prog_label;
static lv_obj_t *g_prog_pct;

/* disk list widgets */
static lv_obj_t *g_disk_rows[16];
static int g_disk_row_count;

/* ===================================================================== */
/*  Forward declarations                                                   */
/* ===================================================================== */

static void show_page(int page);
static void advance(void);
static void go_back(void);

/* ===================================================================== */
/*  Button callbacks                                                       */
/* ===================================================================== */

static void btn_back_cb(lv_event_t *e)
{
    (void)e;
    go_back();
}

static void btn_next_cb(lv_event_t *e)
{
    (void)e;
    advance();
}

/* ===================================================================== */
/*  Page building                                                          */
/* ===================================================================== */

static void build_welcome_page(void)
{
    lv_obj_t *pg = g_pages[XP_WELCOME];

    /* icon */
    lv_obj_t *icon = xenv_icon_tile_create(pg, 20, 30, 64, 64,
                                           "I", NULL,
                                           lv_color_hex(0x1479FF),
                                           lv_color_hex(0x1479FF));

    lv_obj_t *title = lv_label_create(pg);
    lv_label_set_text(title, "Install XKERN");
    lv_obj_set_style_text_color(title, XENV_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_pos(title, 100, 34);

    lv_obj_t *l1 = lv_label_create(pg);
    lv_label_set_text(l1, "This will install the XKERN operating system on a disk.");
    lv_obj_set_style_text_color(l1, XENV_TEXT, 0);
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(l1, 100, 86);

    lv_obj_t *l2 = lv_label_create(pg);
    lv_label_set_text(l2, "No data can be recovered once a disk is erased.");
    lv_obj_set_style_text_color(l2, XENV_GRAY, 0);
    lv_obj_set_style_text_font(l2, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(l2, 100, 112);

    lv_obj_t *l3 = lv_label_create(pg);
    lv_label_set_text(l3, "Keep your computer plugged in during installation.");
    lv_obj_set_style_text_color(l3, XENV_GRAY, 0);
    lv_obj_set_style_text_font(l3, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(l3, 100, 146);

    char buf[96];
    snprintf(buf, sizeof(buf), "Install image embedded: %u MB",
             (xenv_payload_size() + 0xFFFFFu) >> 20);
    lv_obj_t *l4 = lv_label_create(pg);
    lv_label_set_text(l4, buf);
    lv_obj_set_style_text_color(l4, XENV_GRAY, 0);
    lv_obj_set_style_text_font(l4, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(l4, 100, 204);
}

static void disk_row_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_sel = idx;
    /* visual update */
    int i;
    for (i = 0; i < g_disk_row_count && i < 16; i++) {
        if (i == g_sel) {
            lv_obj_set_style_bg_color(g_disk_rows[i], XENV_SEL_BG, 0);
            lv_obj_set_style_bg_opa(g_disk_rows[i], LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_opa(g_disk_rows[i], LV_OPA_TRANSP, 0);
        }
    }
}

static void build_disk_page(void)
{
    lv_obj_t *pg = g_pages[XP_DISK];

    lv_obj_t *title = lv_label_create(pg);
    lv_label_set_text(title, "Select a disk");
    lv_obj_set_style_text_color(title, XENV_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_set_pos(title, 20, 18);

    lv_obj_t *sub = lv_label_create(pg);
    lv_label_set_text(sub, "Choose the disk where XKERN will be installed.");
    lv_obj_set_style_text_color(sub, XENV_GRAY, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(sub, 20, 50);

    /* disk list container */
    int i, n = xenv_disk_count();
    g_disk_row_count = n;
    lv_coord_t ry = 82;

    lv_obj_t *list_bg = lv_obj_create(pg);
    lv_obj_set_size(list_bg, XPANEL_W - 40, n > 0 ? n * 54 - 6 : 48);
    lv_obj_set_pos(list_bg, 20, ry);
    lv_obj_set_style_bg_color(list_bg, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_radius(list_bg, 8, 0);
    lv_obj_set_style_border_color(list_bg, lv_color_hex(0x2A2A30), 0);
    lv_obj_set_style_border_width(list_bg, 1, 0);
    lv_obj_set_scrollbar_mode(list_bg, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(list_bg, LV_OBJ_FLAG_SCROLLABLE);

    ry = 4;
    for (i = 0; i < n && i < 16; i++) {
        const char *name, *model;
        u32 mb;
        char line[96];

        if (xenv_disk_get(i, &name, &model, &mb) != 0) break;

        lv_obj_t *row = lv_obj_create(list_bg);
        lv_obj_set_size(row, XPANEL_W - 48, 48);
        lv_obj_set_pos(row, 4, ry);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, disk_row_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        g_disk_rows[i] = row;

        snprintf(line, sizeof(line), "/dev/%s  %u MB", name, mb);
        lv_obj_t *nmlbl = lv_label_create(row);
        lv_label_set_text(nmlbl, line);
        lv_obj_set_style_text_color(nmlbl, XENV_TEXT, 0);
        lv_obj_set_style_text_font(nmlbl, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(nmlbl, 14, 7);

        lv_obj_t *mdlbl = lv_label_create(row);
        lv_label_set_text(mdlbl, model);
        lv_obj_set_style_text_color(mdlbl, XENV_GRAY, 0);
        lv_obj_set_style_text_font(mdlbl, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(mdlbl, 14, 26);

        ry += 54;
    }
    if (n == 0) {
        lv_obj_t *empty = lv_label_create(pg);
        lv_label_set_text(empty, "No disks found.");
        lv_obj_set_style_text_color(empty, XENV_GRAY, 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(empty, 34, ry + 4);
    }
}

static void build_confirm_page(void)
{
    lv_obj_t *pg = g_pages[XP_CONFIRM];

    lv_obj_t *title = lv_label_create(pg);
    lv_label_set_text(title, "Ready to install");
    lv_obj_set_style_text_color(title, XENV_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_set_pos(title, 20, 18);

    lv_obj_t *l1 = lv_label_create(pg);
    lv_label_set_text(l1, "XKERN will be installed on the disk below. The disk will");
    lv_obj_set_style_text_color(l1, XENV_TEXT, 0);
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(l1, 20, 64);

    lv_obj_t *l2 = lv_label_create(pg);
    lv_label_set_text(l2, "be erased. Click Install to begin.");
    lv_obj_set_style_text_color(l2, XENV_TEXT, 0);
    lv_obj_set_style_text_font(l2, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(l2, 20, 90);

    /* disk info box */
    lv_obj_t *box = lv_obj_create(pg);
    lv_obj_set_size(box, XPANEL_W - 40, 48);
    lv_obj_set_pos(box, 20, 132);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x452828), 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x8A3A3A), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    char line[96];
    snprintf(line, sizeof(line), "/dev/%s  (%s)", g_disk_name, g_disk_model);
    lv_obj_t *nmlbl = lv_label_create(box);
    lv_label_set_text(nmlbl, line);
    lv_obj_set_style_text_color(nmlbl, XENV_TEXT, 0);
    lv_obj_set_style_text_font(nmlbl, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(nmlbl, 14, 6);

    char sz[48];
    snprintf(sz, sizeof(sz), "%u MB available", g_disk_mb);
    lv_obj_t *szlbl = lv_label_create(box);
    lv_label_set_text(szlbl, sz);
    lv_obj_set_style_text_color(szlbl, XENV_GRAY, 0);
    lv_obj_set_style_text_font(szlbl, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(szlbl, 14, 28);
}

static void build_progress_page(void)
{
    lv_obj_t *pg = g_pages[XP_PROGRESS];

    lv_obj_t *title = lv_label_create(pg);
    lv_label_set_text(title, "Installing XKERN");
    lv_obj_set_style_text_color(title, XENV_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_set_pos(title, 20, 18);

    lv_obj_t *sub = lv_label_create(pg);
    lv_label_set_text(sub, "Writing the system to the disk...");
    lv_obj_set_style_text_color(sub, XENV_GRAY, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(sub, 20, 52);

    char line[96];
    snprintf(line, sizeof(line), "/dev/%s", g_disk_name);
    lv_obj_t *dsk = lv_label_create(pg);
    lv_label_set_text(dsk, line);
    lv_obj_set_style_text_color(dsk, XENV_TEXT, 0);
    lv_obj_set_style_text_font(dsk, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(dsk, 20, 96);

    /* progress bar */
    g_prog_bar = lv_bar_create(pg);
    lv_obj_set_size(g_prog_bar, XPANEL_W - 80, 18);
    lv_obj_set_pos(g_prog_bar, 20, 140);
    lv_bar_set_range(g_prog_bar, 0, 100);
    lv_bar_set_value(g_prog_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_prog_bar, lv_color_hex(0x18181E), 0);
    lv_obj_set_style_bg_opa(g_prog_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_prog_bar, 9, 0);
    lv_obj_set_style_bg_main_stop(g_prog_bar, 0, 0);
    lv_obj_set_style_bg_grad_stop(g_prog_bar, 0, 0);
    lv_obj_set_style_bg_color(g_prog_bar, XENV_BLUE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_prog_bar, 9, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_prog_bar, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    /* percentage text */
    g_prog_pct = lv_label_create(pg);
    lv_label_set_text(g_prog_pct, "0%");
    lv_obj_set_style_text_color(g_prog_pct, XENV_GRAY, 0);
    lv_obj_set_style_text_font(g_prog_pct, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(g_prog_pct, 20, 166);

    g_prog_label = lv_label_create(pg);
    lv_label_set_text(g_prog_label, "");
    lv_obj_set_style_text_color(g_prog_label, XENV_GRAY, 0);
    lv_obj_set_style_text_font(g_prog_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(g_prog_label, 20, 186);
}

static void build_done_page(void)
{
    lv_obj_t *pg = g_pages[XP_DONE];

    lv_obj_t *icon = xenv_icon_tile_create(pg, 20, 30, 60, 60,
                                           "OK", NULL,
                                           lv_color_hex(0x2EB04A),
                                           lv_color_hex(0x2EB04A));

    lv_obj_t *title = lv_label_create(pg);
    lv_label_set_text(title, g_result > 0 ? "Installation Succeeded"
                                         : "Installation Failed");
    lv_obj_set_style_text_color(title, XENV_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_set_pos(title, 100, 38);

    if (g_result > 0) {
        lv_obj_t *l1 = lv_label_create(pg);
        lv_label_set_text(l1, "XKERN has been installed on");
        lv_obj_set_style_text_color(l1, XENV_TEXT, 0);
        lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(l1, 100, 84);

        lv_obj_t *l2 = lv_label_create(pg);
        lv_label_set_text(l2, g_disk_name);
        lv_obj_set_style_text_color(l2, XENV_BLUE, 0);
        lv_obj_set_style_text_font(l2, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(l2, 100, 108);

        lv_obj_t *l3 = lv_label_create(pg);
        lv_label_set_text(l3, "Restart to boot the installed system.");
        lv_obj_set_style_text_color(l3, XENV_GRAY, 0);
        lv_obj_set_style_text_font(l3, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(l3, 100, 140);
    } else {
        lv_obj_t *l1 = lv_label_create(pg);
        lv_label_set_text(l1, "The disk could not be written.");
        lv_obj_set_style_text_color(l1, XENV_GRAY, 0);
        lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(l1, 100, 84);
    }
}

/* ===================================================================== */
/*  Navigation                                                             */
/* ===================================================================== */

static void show_page(int page)
{
    int i;
    g_page = page;
    for (i = 0; i < 5; i++) {
        if (i == page)
            lv_obj_clear_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* button visibility */
    switch (page) {
    case XP_WELCOME:
        lv_obj_add_flag(g_btn_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_btn_next, LV_OBJ_FLAG_HIDDEN);
        {
            lv_obj_t *lbl = lv_obj_get_child(g_btn_next, 0);
            if (lbl) lv_label_set_text(lbl, "Continue");
        }
        break;
    case XP_DISK:
        lv_obj_clear_flag(g_btn_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_btn_next, LV_OBJ_FLAG_HIDDEN);
        {
            lv_obj_t *lbl = lv_obj_get_child(g_btn_next, 0);
            if (lbl) lv_label_set_text(lbl, "Continue");
        }
        break;
    case XP_CONFIRM:
        lv_obj_clear_flag(g_btn_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_btn_next, LV_OBJ_FLAG_HIDDEN);
        {
            lv_obj_t *lbl = lv_obj_get_child(g_btn_next, 0);
            if (lbl) lv_label_set_text(lbl, "Install");
        }
        break;
    case XP_PROGRESS:
        lv_obj_add_flag(g_btn_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_btn_next, LV_OBJ_FLAG_HIDDEN);
        break;
    case XP_DONE:
        lv_obj_add_flag(g_btn_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_btn_next, LV_OBJ_FLAG_HIDDEN);
        {
            lv_obj_t *lbl = lv_obj_get_child(g_btn_next, 0);
            if (lbl) lv_label_set_text(lbl, "Restart");
        }
        break;
    }
}

static void begin_install(void)
{
    const char *name, *model;
    u32 mb;

    if (xenv_disk_get(g_sel, &name, &model, &mb) != 0) return;
    snprintf(g_disk_name, sizeof(g_disk_name), "%s", name);
    snprintf(g_disk_model, sizeof(g_disk_model), "%s", model);
    g_disk_mb = mb;
    g_lba = 0;
    g_total_sectors = xenv_payload_size() / 512;
    if (!g_total_sectors || !xenv_payload_present()) {
        g_result = -1;
        show_page(XP_DONE);
        return;
    }
    show_page(XP_PROGRESS);
}

static void advance(void)
{
    switch (g_page) {
    case XP_WELCOME:
        show_page(XP_DISK);
        g_sel = 0;
        break;
    case XP_DISK:
        show_page(XP_CONFIRM);
        break;
    case XP_CONFIRM:
        begin_install();
        break;
    case XP_DONE:
        xenv_reboot();
        break;
    default:
        break;
    }
}

static void go_back(void)
{
    if (g_page == XP_DISK)
        show_page(XP_WELCOME);
    else if (g_page == XP_CONFIRM)
        show_page(XP_DISK);
}

static void write_chunk(void)
{
    const u8 *data = xenv_payload_data();
    u32 n = 64;
    char buf[64];

    if (!data) return;
    if (g_lba + n > g_total_sectors)
        n = g_total_sectors - g_lba;
    if (devfs_write(g_disk_name, g_lba, (u8)n,
                    (void *)(data + (u64)g_lba * 512))) {
        g_result = -1;
        show_page(XP_DONE);
        return;
    }
    g_lba += n;
    if (g_lba >= g_total_sectors) {
        g_result = 1;
        show_page(XP_DONE);
        return;
    }

    /* update progress */
    u32 pct = (u32)(((u64)g_lba * 100) / (g_total_sectors ? g_total_sectors : 1));
    lv_bar_set_value(g_prog_bar, (int32_t)pct, LV_ANIM_OFF);
    snprintf(buf, sizeof(buf), "%u%%  (%u / %u sectors)", pct,
             g_lba, g_total_sectors);
    lv_label_set_text(g_prog_pct, buf);
}

/* ===================================================================== */
/*  Entry point                                                            */
/* ===================================================================== */

int xinstall_run(void)
{
    lv_coord_t sw, sh;
    u32 start;
    u32 frame = 0;

    if (!xenv_payload_present())
        return 0;

    sw = lv_disp_get_hor_res(NULL);
    sh = lv_disp_get_ver_res(NULL);

    /* reset state */
    g_page = XP_WELCOME;
    g_sel = 0;
    g_done = 0;
    g_result = 0;
    g_lba = 0;

    /* build widget tree */
    g_scr = lv_scr_act();
    lv_obj_set_style_bg_color(g_scr, XENV_BG, 0);
    lv_obj_set_style_bg_opa(g_scr, LV_OPA_COVER, 0);

    xenv_wallpaper_create(g_scr);

    /* main panel */
    g_panel = lv_obj_create(g_scr);
    lv_obj_set_size(g_panel, XPANEL_W, XPANEL_H);
    lv_obj_set_pos(g_panel, (sw - XPANEL_W) / 2, (sh - XPANEL_H) / 2 - 10);
    lv_obj_set_style_bg_color(g_panel, lv_color_hex(0x0D0D0D), 0);
    lv_obj_set_style_radius(g_panel, 14, 0);
    lv_obj_set_style_border_color(g_panel, lv_color_hex(0x2A2A30), 0);
    lv_obj_set_style_border_width(g_panel, 1, 0);
    lv_obj_set_scrollbar_mode(g_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(g_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* build all pages inside the panel */
    int i;
    for (i = 0; i < 5; i++) {
        g_pages[i] = lv_obj_create(g_panel);
        lv_obj_set_size(g_pages[i], XPANEL_W - 20, XPANEL_H - 70);
        lv_obj_set_pos(g_pages[i], 10, 10);
        lv_obj_set_style_bg_opa(g_pages[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(g_pages[i], 0, 0);
        lv_obj_set_style_radius(g_pages[i], 0, 0);
        lv_obj_set_scrollbar_mode(g_pages[i], LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(g_pages[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
    }

    build_welcome_page();
    build_disk_page();
    build_confirm_page();
    build_progress_page();
    build_done_page();

    /* buttons at the bottom of the panel */
    lv_coord_t bw = 132;
    lv_coord_t bh = 32;
    lv_coord_t by_ = XPANEL_H - bh - 18;

    g_btn_back = lv_btn_create(g_panel);
    lv_obj_set_size(g_btn_back, bw, bh);
    lv_obj_set_pos(g_btn_back, XPANEL_W - 2 * bw - 12 - 18, by_);
    lv_obj_set_style_bg_color(g_btn_back, lv_color_hex(0x121216), 0);
    lv_obj_set_style_radius(g_btn_back, 6, 0);
    lv_obj_set_style_border_color(g_btn_back, lv_color_hex(0x2A2A30), 0);
    lv_obj_set_style_border_width(g_btn_back, 1, 0);
    {
        lv_obj_t *lbl = lv_label_create(g_btn_back);
        lv_label_set_text(lbl, "Back");
        lv_obj_set_style_text_color(lbl, XENV_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(g_btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);

    g_btn_next = lv_btn_create(g_panel);
    lv_obj_set_size(g_btn_next, bw, bh);
    lv_obj_set_pos(g_btn_next, XPANEL_W - bw - 18, by_);
    lv_obj_set_style_bg_color(g_btn_next, XENV_BLUE, 0);
    lv_obj_set_style_radius(g_btn_next, 6, 0);
    lv_obj_set_style_border_width(g_btn_next, 0, 0);
    {
        lv_obj_t *lbl = lv_label_create(g_btn_next);
        lv_label_set_text(lbl, "Continue");
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(g_btn_next, btn_next_cb, LV_EVENT_CLICKED, NULL);

    show_page(XP_WELCOME);

    /* --- run loop --------------------------------------------------------- */
    klog("xinstall", "entering LVGL run loop");
    start = (u32)tsc_ms();

    while (!g_done) {
        u32 now = (u32)tsc_ms();
        u32 dt = (frame == 0) ? 16 : (now - start - (frame - 1) * 16);
        if (dt > 100) dt = 16;
        frame++;

        lv_port_poll();
        lv_timer_handler();

        /* progress writes happen every frame */
        if (g_page == XP_PROGRESS)
            write_chunk();

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

        /* keyboard */
        if (g_page == XP_PROGRESS) {
            /* no keyboard during install */
        } else {
            if (input_pressed(GKEY_ESC) && g_page != XP_PROGRESS) {
                g_done = 1;
                g_result = 0;
            }
            if (input_pressed(GKEY_ENTER))
                advance();
            if (g_page == XP_DISK) {
                int n = xenv_disk_count() - 1;
                if (input_pressed(GKEY_UP) && g_sel > 0) g_sel--;
                if (input_pressed(GKEY_DOWN) && g_sel < n) g_sel++;
                if (input_pressed(GKEY_LEFT) && g_sel > 0) g_sel--;
                if (input_pressed(GKEY_RIGHT) && g_sel < n) g_sel++;
                /* update visual selection */
                int i;
                for (i = 0; i < g_disk_row_count && i < 16; i++) {
                    if (i == g_sel) {
                        lv_obj_set_style_bg_color(g_disk_rows[i],
                                                  XENV_SEL_BG, 0);
                        lv_obj_set_style_bg_opa(g_disk_rows[i],
                                                LV_OPA_COVER, 0);
                    } else {
                        lv_obj_set_style_bg_opa(g_disk_rows[i],
                                                LV_OPA_TRANSP, 0);
                    }
                }
            }
        }
    }

    /* clean up */
    lv_obj_clean(g_scr);
    input_clear();
    klog("xinstall", "exiting LVGL run loop");

    return g_result;
}
