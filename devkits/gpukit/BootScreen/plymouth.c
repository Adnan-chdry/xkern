/**
 * plymouth.c - XKERN boot splash (Plymouth handler).
 *
 *  Displays "XkOS" in bold text with a rotating spinner on a dark
 *  background while the kernel initialises.  The splash stays visible
 *  on console0 until switch_console() is called from kernel_main.
 *  Config driven by config.h (12 hardcoded values for now).
 *  xkern 26.0.8
 */
#include <xkern/devkits/gpukit/lvgl/src/lv_conf_internal.h>
#include <xkern/osfmk/kern/version.h> //from /usr/include/xkern
#include "plymouth.h"
#include "config.h"
#include "lv_port.h"
#include "lvgl.h"
#include "tsc.h"
#include "klog.h"
#include "IOGraphicsFamily/fb.h"


/*==================================================================*/

/*==================================================================*/


/* ===================================================================== */
/*  Helpers                                                               */
/* ===================================================================== */

static void center_obj(lv_obj_t *obj, lv_coord_t cx, lv_coord_t cy)
{
    lv_obj_update_layout(obj);
    lv_coord_t w = lv_obj_get_width(obj);
    lv_coord_t h = lv_obj_get_height(obj);
    lv_obj_set_pos(obj, cx - w / 2, cy - h / 2);
}

void plymouth_d_print(const char *msg)
{
    lv_obj_t *scr = lv_scr_act();
    if (!scr || !msg)
        return;

    lv_obj_t *bg = lv_obj_create(scr);
    lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t sw = lv_obj_get_width(scr);
    lv_coord_t sh = lv_obj_get_height(scr);

    lv_obj_t *title = lv_label_create(bg);
    lv_label_set_text(title, "KERNEL PANIC");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x8B0000), 0);
    center_obj(title, sw / 2, sh / 2 - 40);

    lv_obj_t *line = lv_label_create(bg);
    lv_label_set_text(line, "-----------------------------------");
    lv_obj_set_style_text_color(line, lv_color_hex(0x660000), 0);
    center_obj(line, sw / 2, sh / 2 - 10);

    lv_obj_t *msgk = lv_label_create(bg);
    lv_label_set_text(msgk, msg);
    lv_obj_set_style_text_font(msgk, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(msgk, lv_color_hex(0x990000), 0);
    lv_label_set_long_mode(msgk, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msgk, sw - 80);
    center_obj(msgk, sw / 2, sh / 2 + 30);

    lv_obj_t *hint = lv_label_create(bg);
    lv_label_set_text(hint, "System halted.");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x660000), 0);
    center_obj(hint, sw / 2, sh / 2 + 80);

    lv_refr_now(NULL);
}


/* ===================================================================== */
/*  Build widget tree                                                     */
/* ===================================================================== */

void build_screen(void)	//removed static label
{
    lv_obj_t *scr;
    lv_coord_t sw = (lv_coord_t)framebuffer_width();
    lv_coord_t sh = (lv_coord_t)framebuffer_height();
    lv_coord_t cx = sw / 2;
    lv_coord_t cy = sh / 2;

    /* scale everything relative to the short edge (baseline = 768) */
    lv_coord_t base = (sw < sh) ? sw : sh;
    lv_coord_t spinner_dia = (lv_coord_t)((base * BOOT_SPINNER_DIA) / 768);
    lv_coord_t arc_w       = (lv_coord_t)((base * 4) / 768);
    if (arc_w < 2) arc_w = 2;
    lv_coord_t gap         = (lv_coord_t)((base * BOOT_SPINNER_Y_OFS) / 768);

    /* --- dark background ----------------------------------------------- */
    scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(BOOT_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* --- "XkOS" title -------------------------------------------------- */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "XkOS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(title, LV_OPA_COVER, 0);
    center_obj(title, cx, cy + BOOT_LOGO_Y_OFS);

    /* --- rotating spinner ---------------------------------------------- */
    lv_obj_t *spinner = lv_spinner_create(scr,
                                          BOOT_SPINNER_TIME,
                                          BOOT_SPINNER_ARC);
    lv_obj_set_size(spinner, spinner_dia, spinner_dia);

    lv_obj_set_style_arc_color(spinner,
                               lv_color_hex(BOOT_SPINNER_COLOR),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, arc_w, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, arc_w, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(spinner, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(spinner, LV_OPA_TRANSP, LV_PART_MAIN);

    /* position spinner below the title, all screen-relative */
    lv_obj_update_layout(title);
    center_obj(spinner, cx,
               lv_obj_get_y(title) + lv_obj_get_height(title)
               + gap + spinner_dia / 2);
    //pointer to sub
    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub,"test plymouth"); //cant use variable based ones const required
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0xf2e1b3), 0);
    lv_obj_set_style_text_opa(sub, LV_OPA_COVER, 0);
    center_obj(sub, cx, cy);
}

/* ===================================================================== */
/*  Public API                                                            */
/* ===================================================================== */

/*
 *  plymouth_run() — non-blocking.  Builds the splash on console0 and
 *  returns immediately.  The splash stays visible until switch_console()
 *  is called.
 */
void plymouth_run(void)
{
    if (!framebuffer_ready())
        return;

    lv_port_init(framebuffer_width(), framebuffer_height());

    build_screen();

    klog("boot.plymouth", "splash built on console0 (%ux%u)",
         framebuffer_width(), framebuffer_height());
}
