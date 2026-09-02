/*
 * usb_irq.h - generic, dynamically-registered IRQ dispatch for the USB
 * stack.
 *
 * The kernel's isr.asm routes each PIC vector (0x20 + irq) through a
 * generated stub that calls kernel_irq_dispatch(vector).  Drivers register
 * a handler here; the dispatcher EOIs the interrupt and invokes it.  This
 * lets a USB host controller use real interrupts instead of polling.
 *
 * Everything is opt-in per driver (the poll path is unchanged unless a
 * driver calls usb_irq_register() + usb_irq_enable()).
 */
#ifndef USB_IRQ_H
#define USB_IRQ_H

#include "types.h"

/* Called from isr.asm for every dispatched vector. */
void kernel_irq_dispatch(u8 vector);

/* Address of the dispatch stub for a given IRQ (for idt_set_gate). */
u64 usb_irq_stub(u8 irq);

/* Register a handler for a PIC IRQ line (0..15).  Returns 0 on success. */
int usb_irq_register(u8 irq, void (*fn)(void *arg), void *arg);

/* Unmask the PIC line so the device's interrupt reaches the CPU. */
void usb_irq_enable(u8 irq);

/* Mask the PIC line again. */
void usb_irq_disable(u8 irq);

#endif /* USB_IRQ_H */
