#ifndef SMP_H
#define SMP_H

#include "types.h"
#include "acpi/acpi.h"

/* ---- LAPIC MMIO defaults ---- */
#define LAPIC_BASE_DEFAULT  0xFEE00000ULL

/* LAPIC register offsets (from MMIO base) */
#define LAPIC_ID            0x20
#define LAPIC_TPR           0x80
#define LAPIC_EOI           0xB0
#define LAPIC_LDR           0xD0
#define LAPIC_SPURIOUS      0xF0
#define LAPIC_ICR_LOW       0x300
#define LAPIC_ICR_HIGH      0x310
#define LAPIC_LVT_LINT0     0x350
#define LAPIC_LVT_LINT1     0x360
#define LAPIC_LVT_ERROR     0x370
#define LAPIC_TDCR          0x3E0

/* ICR delivery modes */
#define ICR_DEL_INIT        5
#define ICR_DEL_SIPI        6
#define ICR_DEL_FIXED       0

/* ICR shorthand */
#define ICR_SHORTHAND_NONE      0
#define ICR_SHORTHAND_SELF      1
#define ICR_SHORTHAND_ALL_INC   2
#define ICR_SHORTHAND_ALL_EXC   3

/* ---- I/O APIC ---- */
#define IOAPIC_REGSEL       0x00
#define IOAPIC_WIN          0x10
#define IOAPIC_ID_REG       0x00
#define IOAPIC_VER_REG      0x01
#define IOAPIC_REDIR_BASE   0x10

/* ---- Trampoline physical layout ---- */
#define TRAMP_PHYS_CODE     0x8000
#define TRAMP_PHYS_GDT      0x8100
#define TRAMP_PHYS_DATA     0x8200

/* ---- Limits ---- */
#define SMP_MAX_CPUS        8

/* Shared trampoline data (at physical 0x8200, identity mapped) */
struct tramp_data {
    u64 stack;      /* AP kernel stack pointer */
    u64 cr3;        /* page table physical address */
    u64 entry;      /* C entry function pointer */
    u32 cpu_id;     /* CPU id for this AP */
    u32 ready;      /* set to 1 by AP once running */
};

/* Per-CPU structure */
struct cpu {
    u8  id;
    u8  apic_id;
    u8  bsp;        /* 1 = bootstrap processor */
    u32 online;
    volatile u32 ready;
    u64 stack;
    u64 cr3;
};

/* ---- LAPIC ---- */
void    lapic_init(void);
void    lapic_enable(void);
void    lapic_eoi(void);
u32     lapic_read(u32 reg);
void    lapic_write(u32 reg, u32 val);
u8      lapic_get_id(void);
void    lapic_send_ipi(u32 dest, u32 vector, u32 delivery, u32 shorthand);

/* ---- I/O APIC ---- */
void    ioapic_init(void);
u32     ioapic_read(u32 reg);
void    ioapic_write(u32 reg, u32 val);
void    ioapic_set_redirect(u8 vector, u8 dest_apic_id, int masked);
void    ioapic_mask_all(void);

/* ---- SMP ---- */
void            smp_init(void);
u32             smp_get_cpu_count(void);
struct cpu *    smp_get_cpu(u8 id);
struct cpu *    smp_current_cpu(void);

#endif
