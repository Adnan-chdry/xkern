#include "pic.h"
#include "io.h"

#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1

void pic_remap(u8 master_offset, u8 slave_offset) {
    u8 a1, a2;

    a1 = inb(PIC1_DATA);
    a2 = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, master_offset);
    outb(PIC2_DATA, slave_offset);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

void pic_enable_irq(u8 irq) {
    if (irq < 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) & ~(1 << irq));
    } else {
        outb(PIC2_DATA, inb(PIC2_DATA) & ~(1 << (irq - 8)));
    }
}

void pic_disable_irq(u8 irq) {
    u16 port;
    u8 mask;

    if (irq <8){
        port =PIC1_DATA;
    }else {
        port = PIC2_DATA;
        irq -= 8;
    }
    mask = inb(port);
    mask |=(1 << irq);
    outb(port, mask);
}

void pic_send_eoi(u8 irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);
    }
    outb(PIC1_CMD, 0x20);
}

void pic_init(void) {
    pic_remap(0x20, 0x28);
    outb(PIC1_DATA, 0xFD);
    outb(PIC2_DATA, 0xFF);
}