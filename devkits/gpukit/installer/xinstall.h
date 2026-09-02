#ifndef XINSTALL_H
#define XINSTALL_H

/*
 * xinstall.h - XKERN installer wizard (LVGL edition).
 *
 * A macOS-styled installer that writes the embedded bootable install image
 * sector-by-sector to a selected block device through devfs.
 * Returns 1 on success, 0 if cancelled.
 */

int xinstall_run(void);

#endif
