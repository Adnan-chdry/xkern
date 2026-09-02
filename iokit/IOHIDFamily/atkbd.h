#ifndef ATKBD_H
#define ATKBD_H

#include "types.h"

#define KBD_BUF_SIZE 256

void atkbd_init(void);
void atkbd_register_irq(void);
void atkbd_handler(void);
void atkbd_poll(void);
int atkbd_is_key_pressed(u8 scancode);
u8 atkbd_get_char(u8 scancode);
int atkbd_getchar(void);
int atkbd_pollchar(void);
void kbd_push(char c);

void irq1_handler(void);

#endif