/*
 * atmouse.c - PS/2 mouse (auxiliary device) driver.
 *
 * The PS/2 controller shares port 0x60 (data) / 0x64 (command) with the
 * keyboard.  Auxiliary-device commands are passed through with 0xD4.  This
 * driver enables the aux channel + IRQ12, then decodes the standard 3-byte
 * packets: status byte (buttons + overflow + sign bits) followed by X and Y
 * motion bytes.  +Y in PS/2 means "away from the user", so callers must
 * flip the sign for screen coordinates (screen Y grows downward).
 */
#include "atmouse.h"
#include "io.h"
#include "pic.h"
#include "idt.h"
#include "klog.h"
#include "pit.h"

#define ATM_TIMEOUT_MS 10000

static volatile s32 g_dx;
static volatile s32 g_dy;
static volatile u8 g_buttons;
static volatile int g_ready;

static volatile u8 g_pkt[3];
static volatile u32 g_pkt_idx;

static uint32_t atmouse_timeout_deadline(void)
{
    return pit_get_ticks() + pit_ms_to_ticks(ATM_TIMEOUT_MS);
}

static int atmouse_expired(uint32_t deadline)
{
    return (int32_t)(pit_get_ticks() - deadline) >= 0;
}

static int atmouse_wait_input(void)
{
    uint32_t deadline = atmouse_timeout_deadline();
    while (!(inb(0x64) & 0x01)) {
        if (atmouse_expired(deadline))
            return -1;
        asm volatile (" pause ");
    }
    return 0;
}

static int atmouse_wait_output(void)
{
    uint32_t deadline = atmouse_timeout_deadline();
    while (inb(0x64) & 0x02) {
        if (atmouse_expired(deadline))
            return -1;
        asm volatile (" pause ");
    }
    return 0;
}

static int atmouse_write_cmd(u8 b)
{
    if (atmouse_wait_output() != 0)
        return -1;
    outb(0x64, b);
    return 0;
}

static int atmouse_write_data(u8 b)
{
    if (atmouse_wait_output() != 0)
        return -1;
    outb(0x60, b);
    return 0;
}

/* send one byte to the auxiliary (mouse) device */
static int atmouse_send(u8 b)
{
    if (atmouse_write_cmd(0xD4) != 0)
        return -1;
    return atmouse_write_data(b);
}

/* wait for the device's ACK (0xFA); 0xFE means "resend this command" */
static int atmouse_wait_ack(void)
{
    uint32_t deadline = atmouse_timeout_deadline();
    while (1) {
        if (atmouse_wait_input() != 0)
            return -1;
        {
            u8 b = inb(0x60);
            if (b == 0xFA)
                return 0;
            if (b == 0xFE)
                continue;
            return -1;   /* self-test / error, no mouse here */
        }
    }
}

/*
 * Feed one byte into the 3-byte packet state machine.  Non-status bytes
 * (ACKs, self-test results, or a keyboard byte that leaked through the
 * shared buffer) are dropped and re-sync happens on the next status byte
 * (bit 3 set, always 1 on a real packet).
 */
static void atmouse_byte(u8 b)
{
    if (g_pkt_idx == 0) {
        if (!(b & 0x08))
            return;
        g_pkt[0] = b;
        g_pkt_idx = 1;
        return;
    }
    if (g_pkt_idx == 1) {
        g_pkt[1] = b;
        g_pkt_idx = 2;
        return;
    }
    g_pkt[2] = b;
    g_pkt_idx = 0;
    {
        u8 flags = g_pkt[0];
        if (!(flags & 0xC0)) {   /* no X/Y overflow */
            s32 dx = (s32)g_pkt[1];
            s32 dy = (s32)g_pkt[2];
            if (flags & 0x10)    /* 9-bit X sign */
                dx -= 256;
            if (flags & 0x20)    /* 9-bit Y sign */
                dy -= 256;
            g_dx += dx;
            g_dy += dy;
        }
        g_buttons = flags & 0x07;
    }
}

void atmouse_handler_irq12(void)
{
    atmouse_byte(inb(0x60));
    pic_send_eoi(12);
}

void atmouse_poll(void)
{
    uint32_t deadline = atmouse_timeout_deadline();
    while (inb(0x64) & 0x01) {
        atmouse_byte(inb(0x60));
        if (atmouse_expired(deadline))
            break;
    }
}

int atmouse_ready(void)
{
    return g_ready;
}

int atmouse_sample(int *dx, int *dy, u8 *buttons)
{
    if (!g_ready)
        return 0;
    asm volatile ("cli");
    if (dx)
        *dx = (int)g_dx;
    if (dy)
        *dy = (int)g_dy;
    if (buttons)
        *buttons = g_buttons;
    g_dx = 0;
    g_dy = 0;
    asm volatile ("sti");
    return 1;
}

void atmouse_init(void)
{
    u8 cfg;

    g_dx = 0;
    g_dy = 0;
    g_buttons = 0;
    g_pkt_idx = 0;
    g_ready = 0;

    /* take both channels down while we reconfigure */
    if (atmouse_write_cmd(0xA7) != 0)   /* aux disable */
        goto fail;
    atmouse_write_cmd(0xAD);            /* kbd disable */

    /* drop stale bytes that accumulated before the IRQ was installed */
    while (inb(0x64) & 0x01)
        inb(0x60);

    /* controller config: enable the aux IRQ + clocks */
    if (atmouse_write_cmd(0x20) != 0)
        goto fail;
    if (atmouse_wait_input() != 0)
        goto fail;
    cfg = inb(0x60);
    cfg |= 0x02;        /* aux (mouse) IRQ enable */
    cfg &= (u8)~0x20;   /* aux clock enable */
    cfg &= (u8)~0x40;   /* kbd clock enable */
    if (atmouse_write_cmd(0x60) != 0)
        goto fail;
    if (atmouse_write_data(cfg) != 0)
        goto fail;

    if (atmouse_write_cmd(0xA8) != 0)   /* aux enable */
        goto fail;

    /* defaults, then enable data reporting */
    if (atmouse_send(0xF6) != 0)
        goto fail;
    if (atmouse_wait_ack() != 0)
        goto fail;
    if (atmouse_send(0xF4) != 0)
        goto fail;
    if (atmouse_wait_ack() != 0)
        goto fail;

    atmouse_write_cmd(0xAE);            /* kbd re-enable */
    g_ready = 1;
    klog("atmouse", "atmouse_init() done, PS/2 mouse enabled");
    return;

fail:
    atmouse_write_cmd(0xAE);
    klog("atmouse", "atmouse_init(): no PS/2 mouse detected");
}

void atmouse_register_irq(void)
{
    idt_set_gate(0x2C, (u64)irq12_handler, 0x08, 0x8E);
}
