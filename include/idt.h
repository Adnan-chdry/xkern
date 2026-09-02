#ifndef IDT_H
#define IDT_H

#include "types.h"

#define IDT_ENTRIES 256

/* 64-bit interrupt gate descriptor */
struct idt_entry {
    u16 offset_low;     /* bits 0..15 of handler */
    u16 sel;            /* code segment selector */
    u8  ist;            /* IST index (0 = none) */
    u8  flags;          /* type/attributes */
    u16 offset_mid;     /* bits 16..31 */
    u32 offset_high;    /* bits 32..63 */
    u32 reserved;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

extern struct idt_entry idt[IDT_ENTRIES];
extern struct idt_ptr idtp;

void idt_set_gate(u8 num, u64 base, u16 sel, u8 flags);
void idt_load(void);
void idt_init(void);

extern void irq0_handler(void);
extern void irq0_resume(void);
extern void irq1_handler(void);
extern void irq12_handler(void);
extern void syscall_handler(void);
extern void sched_switch(void);
extern void sched_start(void);

extern void exc0_handler(void);
extern void exc1_handler(void);
extern void exc2_handler(void);
extern void exc3_handler(void);
extern void exc4_handler(void);
extern void exc5_handler(void);
extern void exc6_handler(void);
extern void exc7_handler(void);
extern void exc8_handler(void);
extern void exc9_handler(void);
extern void exc10_handler(void);
extern void exc11_handler(void);
extern void exc12_handler(void);
extern void exc13_handler(void);
extern void exc14_handler(void);
extern void exc15_handler(void);
extern void exc16_handler(void);
extern void exc17_handler(void);
extern void exc18_handler(void);
extern void exc19_handler(void);
extern void exc20_handler(void);
extern void exc21_handler(void);
extern void exc22_handler(void);
extern void exc23_handler(void);
extern void exc24_handler(void);
extern void exc25_handler(void);
extern void exc26_handler(void);
extern void exc27_handler(void);
extern void exc28_handler(void);
extern void exc29_handler(void);
extern void exc30_handler(void);
extern void exc31_handler(void);

extern void irq_default_0(void);
extern void irq_default_1(void);
extern void irq_default_2(void);
extern void irq_default_3(void);
extern void irq_default_4(void);
extern void irq_default_5(void);
extern void irq_default_6(void);
extern void irq_default_7(void);
extern void irq_default_8(void);
extern void irq_default_9(void);
extern void irq_default_10(void);
extern void irq_default_11(void);
extern void irq_default_12(void);
extern void irq_default_13(void);
extern void irq_default_14(void);
extern void irq_default_15(void);

#endif
