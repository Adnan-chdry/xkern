#ifndef ATMouse_H
#define ATMouse_H

#include "types.h"

void atmouse_init(void);
void atmouse_register_irq(void);
void atmouse_poll(void);

/* 1 if a PS/2 mouse answered the init sequence */
int atmouse_ready(void);

/*
 * Drain accumulated deltas + button state since the last call.
 * dx/dy are pixel deltas in screen space (+x right, +y down).
 * buttons is a bitmask: bit0 left, bit1 right, bit2 middle.
 * Returns 1 when a mouse is present.
 */
int atmouse_sample(int *dx, int *dy, u8 *buttons);

void irq12_handler(void);
void atmouse_handler_irq12(void);

#endif
