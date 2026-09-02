/*
 * e1000.h - Intel 8254x (e1000) gigabit ethernet driver.
 */
#pragma once
#include "ionet.h"
#include "netdev.h"

#define E1000_VENDOR 0x8086
#define E1000_DEV_82540EM 0x100E
#define E1000_DEV_82545EM 0x100F
#define E1000_DEV_82567LM 0x10EA   /* QEMU e1000-82545em variants */

int  e1000_init(void);          /* probe + bring up; returns IOSVC_OK/-1 */
void e1000_exit(void);
void e1000_poll(void);          /* rx drain, call from ionet_poll() */
