/**
 *  MAIN kernel_main background proc system handler called boot but its plymouth handler
 *  Reads config.h it will auto generated in future but for now its 'hard_coded'{12}
 *  uses smp to spawn kernel_main in cpu1 and cpu0 will handle the plymouth
 *  xkern 26.0.8
 */

#include "boot.h"
#include "config.h"
#include "plymouth.h"
#include "background_tasks.h"
#include "klog.h"
#include "smp.h"
#include "types.h"
#include "kernel.h"
#include "multiboot.h"
#include "IOGraphicsFamily/fb.h"
#include "lvgl.h"

/* ===================================================================== */
/*  State                                                                 */
/* ===================================================================== */

static int g_boot_phase;   /* 0 = splash, 1 = running, 2 = done */

extern void multiboot2_parse(u64 mbi_phys, struct multiboot_info *out);
extern void tsc_init(void);

/* ===================================================================== */
/*  Early framebuffer init (before kernel_main)                           */
/* ===================================================================== */

static void early_fb_init(u64 mbi_phys)
{
    struct multiboot_info mbi;
    multiboot2_parse(mbi_phys, &mbi);

    if (!(mbi.flags & MULTIBOOT_FRAMEBUFFER) ||
        mbi.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT ||
        mbi.framebuffer_pitch == 0) {
        klog("boot", "no framebuffer available, skipping splash");
        return;
    }

    framebuffer_init(
        mbi.framebuffer_addr,
        mbi.framebuffer_width,
        mbi.framebuffer_height,
        mbi.framebuffer_pitch,
        mbi.framebuffer_bpp,
        mbi.framebuffer_red_mask_size,
        mbi.framebuffer_red_field_position,
        mbi.framebuffer_green_mask_size,
        mbi.framebuffer_green_field_position,
        mbi.framebuffer_blue_mask_size,
        mbi.framebuffer_blue_field_position
    );

    framebuffer_clear(0x000000);

    klog("boot", "early framebuffer: %ux%u %u bpp @ 0x%llx",
         mbi.framebuffer_width, mbi.framebuffer_height,
         mbi.framebuffer_bpp, (unsigned long long)mbi.framebuffer_addr);
}

/* ===================================================================== */
/*  Console switch                                                        */
/* ===================================================================== */

/*
 *  Console 0 – plymouth splash (built by plymouth_run on lv_scr_act).
 *  Console 1 – kernel text console (built by lv_console_start).
 *
 *  Console1 is created early so lv_console_start() builds its text
 *  label there.  We switch back to console0 (splash) afterwards so
 *  the splash stays visible.  switch_console() loads console1 when
 *  kernel_main is ready.
 */
static lv_obj_t *g_console0;
static lv_obj_t *g_console1;

void switch_console(void)
{
    if (!g_console1)
        return;   /* plymouth disabled — klog already on the active screen */
    lv_scr_load(g_console1);
    lv_obj_clean(g_console0);
    klog("boot", "console0 -> console1 (splash dismissed)");
}

void restore_console0(void)
{
    if (!g_console0)
        return;   /* plymouth disabled — nothing to restore */
    lv_scr_load(g_console0);
}

static void create_console1(void)
{
    /* save the splash screen */
    g_console0 = lv_scr_act();

    /* create console1 (blank black screen) */
    g_console1 = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_console1, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_console1, LV_OPA_COVER, 0);

    /* temporarily load console1 so lv_console_start() builds on it */
    lv_scr_load(g_console1);
}

/* ===================================================================== */
/*  SMP-aware boot entry                                                  */
/* ===================================================================== */

#include "version.h"
#include "klibc.h"

void boot_run(u64 mbi_phys)
{
    klibc.printf("%s <%s>\n",version,arch);
    klog("boot", "config: logo %dx%d, spinner %dms arc %d deg",
         BOOT_LOGO_DISP_W, BOOT_LOGO_DISP_H,
         BOOT_SPINNER_TIME, BOOT_SPINNER_ARC);

    g_boot_phase = 0;

    /* TSC must be calibrated first — plymouth_run() and LVGL both need tsc_ms() */
    tsc_init();

    /* early framebuffer init so the splash has something to draw to */
    early_fb_init(mbi_phys);

    /* kick off background tasks */
    bg_tasks_init();

    /* SMP dispatch placeholder */
    if (smp_get_cpu_count() > 1) {
        struct cpu *ap = smp_get_cpu(1);
        if (ap && ap->online)
            klog("boot", "AP cpu1 online (dispatch not yet active)");
    }

    /* build the plymouth splash on console0 (non-blocking) */
    if (BOOT_ENABLE_PLYMOUTH) {
        plymouth_run();

        /*
         * Create console1 and temporarily load it so that lv_console_start()
         * (called from kernel_main → kernel_framebuff_init) builds its text
         * label on console1 instead of console0.
         */
        create_console1();
        klog("boot", "splash, console1 ready, handing off to kernel_main");
    } else {
        klog("boot", "plymouth disabled — kernel text renders directly");
    }

    g_boot_phase = 1;

    /* hand off to kernel_main — console1 is active for lv_console to build on */
    kernel_main(mbi_phys);
}

int boot_phase(void)
{
    return g_boot_phase;
}
