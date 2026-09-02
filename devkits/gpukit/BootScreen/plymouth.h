#ifndef PLYMOUTH_H
#define PLYMOUTH_H

/*
 *  plymouth.h - boot splash API.
 *  xkern 26.0.8
 */

/* Run the boot splash (blocks for BOOT_MIN_DURATION ms). */
void plymouth_run(void);

#endif /* PLYMOUTH_H */
