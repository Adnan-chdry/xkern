#include "idt.h"

struct idt_entry idt[IDT_ENTRIES];
struct idt_ptr idtp;

void idt_set_gate(u8 num, u64 base, u16 sel, u8 flags) {
    idt[num].offset_low = base & 0xFFFF;
    idt[num].offset_mid = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (u32)(base >> 32);
    idt[num].sel = sel;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].reserved = 0;
}

void idt_load(void) {
    idtp.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idtp.base = (u64)&idt;
    asm volatile ("lidt %0" : : "m"(idtp));
}

void idt_init(void) {
    u32 i;
    idtp.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idtp.base = (u64)&idt;
    for (i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0x08, 0);
    }
    idt_set_gate(0x00, (u64)exc0_handler, 0x08, 0x8E);
    idt_set_gate(0x01, (u64)exc1_handler, 0x08, 0x8E);
    idt_set_gate(0x02, (u64)exc2_handler, 0x08, 0x8E);
    idt_set_gate(0x03, (u64)exc3_handler, 0x08, 0x8E);
    idt_set_gate(0x04, (u64)exc4_handler, 0x08, 0x8E);
    idt_set_gate(0x05, (u64)exc5_handler, 0x08, 0x8E);
    idt_set_gate(0x06, (u64)exc6_handler, 0x08, 0x8E);
    idt_set_gate(0x07, (u64)exc7_handler, 0x08, 0x8E);
    idt_set_gate(0x08, (u64)exc8_handler, 0x08, 0x8E);
    idt_set_gate(0x09, (u64)exc9_handler, 0x08, 0x8E);
    idt_set_gate(0x0A, (u64)exc10_handler, 0x08, 0x8E);
    idt_set_gate(0x0B, (u64)exc11_handler, 0x08, 0x8E);
    idt_set_gate(0x0C, (u64)exc12_handler, 0x08, 0x8E);
    idt_set_gate(0x0D, (u64)exc13_handler, 0x08, 0x8E);
    idt_set_gate(0x0E, (u64)exc14_handler, 0x08, 0x8E);
    idt_set_gate(0x0F, (u64)exc15_handler, 0x08, 0x8E);
    idt_set_gate(0x10, (u64)exc16_handler, 0x08, 0x8E);
    idt_set_gate(0x11, (u64)exc17_handler, 0x08, 0x8E);
    idt_set_gate(0x12, (u64)exc18_handler, 0x08, 0x8E);
    idt_set_gate(0x13, (u64)exc19_handler, 0x08, 0x8E);
    idt_set_gate(0x14, (u64)exc20_handler, 0x08, 0x8E);
    idt_set_gate(0x15, (u64)exc21_handler, 0x08, 0x8E);
    idt_set_gate(0x16, (u64)exc22_handler, 0x08, 0x8E);
    idt_set_gate(0x17, (u64)exc23_handler, 0x08, 0x8E);
    idt_set_gate(0x18, (u64)exc24_handler, 0x08, 0x8E);
    idt_set_gate(0x19, (u64)exc25_handler, 0x08, 0x8E);
    idt_set_gate(0x1A, (u64)exc26_handler, 0x08, 0x8E);
    idt_set_gate(0x1B, (u64)exc27_handler, 0x08, 0x8E);
    idt_set_gate(0x1C, (u64)exc28_handler, 0x08, 0x8E);
    idt_set_gate(0x1D, (u64)exc29_handler, 0x08, 0x8E);
    idt_set_gate(0x1E, (u64)exc30_handler, 0x08, 0x8E);
    idt_set_gate(0x1F, (u64)exc31_handler, 0x08, 0x8E);
    idt_set_gate(0x20, (u64)irq_default_0, 0x08, 0x8E);
    idt_set_gate(0x21, (u64)irq_default_1, 0x08, 0x8E);
    idt_set_gate(0x22, (u64)irq_default_2, 0x08, 0x8E);
    idt_set_gate(0x23, (u64)irq_default_3, 0x08, 0x8E);
    idt_set_gate(0x24, (u64)irq_default_4, 0x08, 0x8E);
    idt_set_gate(0x25, (u64)irq_default_5, 0x08, 0x8E);
    idt_set_gate(0x26, (u64)irq_default_6, 0x08, 0x8E);
    idt_set_gate(0x27, (u64)irq_default_7, 0x08, 0x8E);
    idt_set_gate(0x28, (u64)irq_default_8, 0x08, 0x8E);
    idt_set_gate(0x29, (u64)irq_default_9, 0x08, 0x8E);
    idt_set_gate(0x2A, (u64)irq_default_10, 0x08, 0x8E);
    idt_set_gate(0x2B, (u64)irq_default_11, 0x08, 0x8E);
    idt_set_gate(0x2C, (u64)irq_default_12, 0x08, 0x8E);
    idt_set_gate(0x2D, (u64)irq_default_13, 0x08, 0x8E);
    idt_set_gate(0x2E, (u64)irq_default_14, 0x08, 0x8E);
    idt_set_gate(0x2F, (u64)irq_default_15, 0x08, 0x8E);
    idt_load();
}
