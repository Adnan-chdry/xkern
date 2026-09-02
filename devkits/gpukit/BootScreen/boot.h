#ifndef BOOT_H
#define BOOT_H

/*
 *  boot.h - boot handler API.
 *  xkern 26.0.8
 */

#include "types.h"

/* Run the full boot sequence (splash → kernel_main). */
void boot_run(u64 mbi_phys);

/* Switch from console0 (plymouth splash) to console1 (kernel text). */
void switch_console(void);

/* Temporarily restore console0 (splash) after lv_console built on console1. */
void restore_console0(void);

/* Current phase: 0 = splash, 1 = running, 2 = done. */
int  boot_phase(void);

#endif /* BOOT_H */
