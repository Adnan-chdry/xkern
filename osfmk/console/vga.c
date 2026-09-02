#include "vga.h"
#include "serial.h"
#include "klog.h"
#include "io.h"

static u16 *vga_buffer;
static u32 terminal_row;
static u32 terminal_col;
static u8  terminal_attribute;

static void vga_update_cursor(void) {
    u16 pos = (u16)(terminal_row * VGA_WIDTH + terminal_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)((pos >> 8) & 0xFF));
}

void vga_init(void) {
    vga_buffer = (u16 *)VGA_ADDR;
    terminal_row = 0;
    terminal_col = 0;
    terminal_attribute = 0x07;
    //vga_clear();
    vga_update_cursor();
    serial_init();
    serial_print("\n");
    klog("lib_vga","kernel_vga loading");
}

static void vga_scroll(void) {
    u32 i;
    for (i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    }
    for (i = VGA_WIDTH * (VGA_HEIGHT - 1); i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = 0x0020 | ((u16)terminal_attribute << 8);
    }
    terminal_row = VGA_HEIGHT - 1;
    vga_update_cursor();
}

void vga_putchar(char c) {
    serial_putchar(c);
    if (c == '\n') {
        terminal_col = 0;
        terminal_row++;
    } else if (c == '\r') {
        terminal_col = 0;
    } else {
        u32 offset = terminal_row * VGA_WIDTH + terminal_col;
        vga_buffer[offset] = (u16)c | ((u16)terminal_attribute << 8);
        terminal_col++;
    }

    if (terminal_col >= VGA_WIDTH) {
        terminal_col = 0;
        terminal_row++;
    }

    if (terminal_row >= VGA_HEIGHT) {
        vga_scroll();
    }
    vga_update_cursor();
}

void vga_backspace(void) {
    if (terminal_col > 0) {
        terminal_col--;
    } else if (terminal_row > 0) {
        terminal_row--;
        terminal_col = VGA_WIDTH - 1;
    } else {
        return;
    }
    u32 offset = terminal_row * VGA_WIDTH + terminal_col;
    vga_buffer[offset] = (u16)' ' | ((u16)terminal_attribute << 8);
    vga_update_cursor();
}

void vga_print(const char *str) {
    while (*str) {
        vga_putchar(*str);
        str++;
    }
}

void vga_clear(void) {
    u32 i;
    for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = 0x0020 | ((u16)terminal_attribute << 8);
    }
    terminal_row = 0;
    terminal_col = 0;
    vga_update_cursor();
}
