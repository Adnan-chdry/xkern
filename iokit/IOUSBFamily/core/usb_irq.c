/*
 * usb_irq.c - generic IRQ dispatch table for the USB stack.
 *
 * The CPU's PIC maps IRQ line N to vector 0x20 + N (see pic_remap).
 * isr.asm sends each such vector to kernel_irq_dispatch(), which looks
 * up the registered handler, runs it, then acks the interrupt the same
 * way the dedicated keyboard/mouse stubs do (LAPIC EOI + PIC EOI).
 */
#include "usb_irq.h"
#include "io.h"
#include "klog.h"

#ifndef LAPIC_EOI_REG
#define LAPIC_EOI_REG 0xFEE000B0ULL
#endif

#define USB_IRQ_MAX 16

struct usb_irq_handler {
    void (*fn)(void *arg);
    void  *arg;
    int    used;
};

static struct usb_irq_handler g_irq[USB_IRQ_MAX];

/* Address of the per-IRQ dispatch stub, provided by isr.asm so drivers
 * can install the matching IDT gate for their IRQ line. */
extern u64 kernel_irq_stubs[USB_IRQ_MAX];

u64 usb_irq_stub(u8 irq)
{
    if (irq >= USB_IRQ_MAX)
        return 0;
    return kernel_irq_stubs[irq];
}

void kernel_irq_dispatch(u8 vector)
{
    /* Spurious / unknown vector: still EOI so the PIC does not wedge. */
    u8 irq = vector - 0x20;

    if (irq < USB_IRQ_MAX && g_irq[irq].used)
        g_irq[irq].fn(g_irq[irq].arg);

    /* Ack: LAPIC EOI then cascade PIC EOI (matches irq0/irq1 stubs). */
    *(volatile u32 *)LAPIC_EOI_REG = 0;
    if (irq >= 8)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

int usb_irq_register(u8 irq, void (*fn)(void *arg), void *arg)
{
    if (irq >= USB_IRQ_MAX || !fn)
        return -1;
    g_irq[irq].fn = fn;
    g_irq[irq].arg = arg;
    g_irq[irq].used = 1;
    return 0;
}

void usb_irq_enable(u8 irq)
{
    extern void pic_enable_irq(u8);
    pic_enable_irq(irq);
}

void usb_irq_disable(u8 irq)
{
    extern void pic_disable_irq(u8);
    pic_disable_irq(irq);
}
