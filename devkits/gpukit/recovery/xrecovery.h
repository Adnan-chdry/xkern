#ifndef XRECOVERY_H
#define XRECOVERY_H

/*
 * xrecovery.h - macOS-styled recovery environment for XKERN.
 *
 * A full-screen, frameless GUI (built on GPUkit's compositor) that boots
 * before the OS proper and offers recovery utilities, mirroring the macOS
 * Recovery experience.  The main menu is a vertical utility list:
 *
 *   - Install XKERN       launches the installer wizard (xinstall.c)
 *   - Disk Utility        inspect / verify / erase attached disks
 *   - Terminal            skip the desktop, userland console only
 *
 * The menu bar additionally offers Boot XKERN (run the desktop, then
 * userland init) and Restart / Shut Down power control.
 *
 * Keyboard: UP/DOWN to move, ENTER to activate, ESC quits to the desktop.
 * Mouse: hover + click drives the utility list and the menu bar.
 */

#define XRECOVERY_DESKTOP  0   /* launch the desktop environment */
#define XRECOVERY_TEXT     1   /* skip the desktop, userland console only */

int xrecovery_run(void);

#endif
