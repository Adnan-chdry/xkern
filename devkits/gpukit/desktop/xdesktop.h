#ifndef XDESKTOP_H
#define XDESKTOP_H

/*
 * xdesktop.h - the XKERN desktop environment (macOS-styled).
 *
 * Runs a full desktop session on the GPUkit compositor: animated wallpaper,
 * menu bar with status items + clock, a magnifying dock and desktop icons,
 * plus the built-in apps (Finder, System Settings, About This Mac,
 * Terminal and the XKERN DOOM raycaster).
 *
 * Returns when the user quits the session (ESC).
 */

void xdesktop_run(void);

#endif
