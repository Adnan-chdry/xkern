#include "serial.h"
#include "io.h"
#include "klog.h"

#define COM1 0x3F8

void serial_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    outb(COM1 + 5, 0x01);
    klog("srl","serial_init() done");
}

void serial_putchar(char c)
{
    while (!(inb(COM1 + 5) & 0x20));
    outb(COM1, (u8)c);
}

void serial_print(const char *str)
{
    while (*str)
        serial_putchar(*str++);
}

int serial_getchar(void)
{
    if (!(inb(COM1 + 5) & 0x01))
        return -1;
    return inb(COM1);
}
