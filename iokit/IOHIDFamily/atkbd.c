#include "atkbd.h"
#include "logger.h"
#include "vga.h"
#include "types.h"
#include "io.h"
#include "pic.h"
#include "idt.h"
#include "klog.h"
#include "pit.h"

#define ATKBD_TIMEOUT_MS 10000

static u8 g_keymap[128];
static u8 g_keymap_shift[128];
static u8 g_key_state[128];
static int g_shift = 0;

static volatile char g_kbd_buf[KBD_BUF_SIZE];
static volatile u32 g_kbd_head;
static volatile u32 g_kbd_tail;

static const u8 scancode_set1[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, 0,
    0, 0, 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-', 0, '+', 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '*', 0, 0, 0, 0, 0, 0, 0, 0,
};

static const u8 scancode_set1_shift[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, 0,
    0, 0, 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-', 0, '+', 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, '*', 0, 0, 0, 0, 0, 0, 0, 0,
};

void kbd_push(char c)
{
    u32 next = (g_kbd_head + 1) % KBD_BUF_SIZE;
    if (next != g_kbd_tail)
    {
        g_kbd_buf[g_kbd_head] = c;
        g_kbd_head = next;
    }
}

static uint32_t atkbd_timeout_deadline(void) {
    return pit_get_ticks() + pit_ms_to_ticks(ATKBD_TIMEOUT_MS);
}

static int atkbd_expired(uint32_t deadline) {
    return (int32_t)(pit_get_ticks() - deadline) >= 0;
}

static int atkbd_flush(void) {
    uint32_t deadline = atkbd_timeout_deadline();
    while (inb(0x64) & 0x01) {
        inb(0x60);
        if (atkbd_expired(deadline))
            return -1;
    }
    return 0;
}

static int atkbd_wait_input(void) {
    uint32_t deadline = atkbd_timeout_deadline();
    while (!(inb(0x64) & 0x01)) {
        if (atkbd_expired(deadline))
            return -1;
        asm volatile (" pause ");
    }
    return 0;
}

static int atkbd_send_cmd(u8 cmd) {
    if (atkbd_flush() != 0)
        return -1;
    outb(0x64, cmd);
    return 0;
}

static int atkbd_write_data(u8 data) {
    if (atkbd_flush() != 0)
        return -1;
    outb(0x60, data);
    return 0;
}

void atkbd_init(void) {
    u32 i;

    for (i = 0; i < 128; i++) {
        g_keymap[i] = scancode_set1[i];
        g_keymap_shift[i] = scancode_set1_shift[i];
        g_key_state[i] = 0;
    }

    g_kbd_head = 0;
    g_kbd_tail = 0;
    g_shift = 0;

    if (atkbd_send_cmd(0xAD) != 0) {
        klog("atkbd", "atkbd_init(): controller not responding (timeout)");
        return;
    }

    atkbd_send_cmd(0xA7);
    atkbd_send_cmd(0x20);

    if (atkbd_wait_input() != 0) {
        klog("atkbd", "atkbd_init(): no response to identify (timeout)");
        return;
    }

    u8 status = inb(0x60);
    status |= 0x02;
    atkbd_send_cmd(0x60);
    atkbd_write_data(status);
    atkbd_send_cmd(0xAE);
    atkbd_send_cmd(0xA8);
    atkbd_write_data(0xF4);
    atkbd_wait_input();

    klog("atkbd","atkbd_init() done");
}

void atkbd_handler_irq1(void) {
    atkbd_handler();
    pic_send_eoi(1);
}

void atkbd_register_irq(void) {
    idt_set_gate(0x21, (u64)irq1_handler, 0x08, 0x8E);
}

static void atkbd_process_scancode(u8 scancode) {
    u8 make = scancode & 0x7F;
    u8 pressed = !(scancode & 0x80);

    if (make == 0x2A || make == 0x36) {
        g_shift = pressed;
        return;
    }

    if (make < 128) {
        g_key_state[make] = pressed ? 1 : 0;
        if (pressed) {
            u8 c = g_shift ? g_keymap_shift[make] : g_keymap[make];
            if (c) {
                vga_putchar(c);
                kbd_push(c);
            } else if (make == 0x1C) {
                vga_putchar('\n');
                kbd_push('\n');
            } else if (make == 0x0E) {
                vga_backspace();
                kbd_push('\b');
            }
        }
    }
}

void atkbd_handler(void) {
    u8 scancode = inb(0x60);
    atkbd_process_scancode(scancode);
}

void atkbd_poll(void) {
    uint32_t deadline = atkbd_timeout_deadline();
    while (inb(0x64) & 0x01) {
        u8 scancode = inb(0x60);
        atkbd_process_scancode(scancode);
        if (atkbd_expired(deadline))
            break;
    }
}

int atkbd_is_key_pressed(u8 scancode) {
    if (scancode < 128) {
        return g_key_state[scancode];
    }
    return 0;
}

u8 atkbd_get_char(u8 scancode) {
    if (scancode < 128) {
        return g_keymap[scancode];
    }
    return 0;
}

int atkbd_getchar(void)
{
    if (g_kbd_tail == g_kbd_head)
        return -1;
    char c = g_kbd_buf[g_kbd_tail];
    g_kbd_tail = (g_kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}

int atkbd_pollchar(void)
{
    int c;
    while ((c = atkbd_getchar()) == -1) {
        asm volatile ("pause; hlt");
    }
    return c;
}
