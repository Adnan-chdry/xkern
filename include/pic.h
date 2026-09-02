#ifndef PIC_H
#define PIC_H

#include "types.h"

void pic_remap(u8 master_offset, u8 slave_offset);
void pic_enable_irq(u8 irq);
void pic_disable_irq(u8 irq);
void pic_send_eoi(u8 irq);
void pic_init(void);

#endif