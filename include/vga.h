#ifndef VGA_H
#define VGA_H

#include "types.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_ADDR   0xB8000

void vga_init(void);
void vga_putchar(char c);
void vga_backspace(void);
void vga_print(const char *str);
void vga_clear(void);

#endif