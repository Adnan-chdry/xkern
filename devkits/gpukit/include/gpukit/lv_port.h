#ifndef LV_PORT_H
#define LV_PORT_H

/*
 * lv_port.h - LVGL port layer for the XKERN kernel.
 *
 * Provides display flush (framebuffer), input (PS/2 mouse + AT keyboard),
 * and tick (tsc_ms).  Call lv_port_init() once, then lv_port_poll() each
 * frame before lv_timer_handler().
 */

#include "types.h"

void lv_port_init(u32 width, u32 height);
void lv_port_poll(void);

#endif
