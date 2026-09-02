/**
 * lv_console.h - LVGL-based kernel console.
 *
 * Renders kernel text output (printf / klog) in an LVGL label on the
 * framebuffer.  When no framebuffer is available the printf layer falls
 * back to the VGA text console automatically (lv_console_active() = 0).
 */
#pragma once
#include "types.h"

int  lv_console_start(u32 width, u32 height);
void lv_console_putc(char c);
void lv_console_pump(void);
void lv_console_settle(void);
int  lv_console_active(void);

/* Tear down the LVGL console (delete its label + stop the PIT pump).
 * Call this before handing the screen to a full GUI/DE so the console's
 * per-tick screen-scroll no longer fights the desktop for the display. */
void lv_console_stop(void);
