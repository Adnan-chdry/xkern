#ifndef XKOS_DE_H
#define XKOS_DE_H

/*
 * xkos_de.h - XKOS desktop environment entry points.
 */
#include "types.h"

void xkos_de_init(void);   /* bring up the D-Bus bus + scaling */
void xkos_de_run(void);    /* build the DE and enter the run loop */

#endif
