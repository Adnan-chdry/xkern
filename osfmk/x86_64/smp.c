#include "smp.h"
#include "pmm.h"
#include "paging.h"
#include "acpi/acpi.h"
#include "klog.h"
#include "pic.h"
#include "idt.h"

asm(
    ".section .rodata\n"
    ".global _trampoline_start\n"
    "_trampoline_start:\n"
    ".incbin \"osfmk/x86_64/trampoline.bin\"\n"
    ".global _trampoline_end\n"
    "_trampoline_end:\n"
    ".section .text\n"
);
extern char _trampoline_start[];
extern char _trampoline_end[];

static struct cpu cpus[SMP_MAX_CPUS];
static u32 cpu_count;
static struct cpu *bsp_cpu;

static u8 ap_stacks[SMP_MAX_CPUS][8192]
    __attribute__((aligned(16)));

struct tramp_data *tramp = (struct tramp_data *)TRAMP_PHYS_DATA;

static void delay_ms(u32 ms)
{
    for (u32 i = 0; i < ms; i++)
        for (volatile u32 j = 0; j < 200000; j++)
            asm volatile("pause");
}

static void lapic_wait_delivery(void)
{
    for (u32 i = 0; i < 1000000; i++) {
        if (!(lapic_read(LAPIC_ICR_LOW) & (1 << 12)))
            return;
        asm volatile("pause");
    }
    klog("smp", "WARNING: LAPIC ICR delivery status stuck");
}

static void ap_entry(void)
{
    struct cpu *c = &cpus[tramp->cpu_id];
    c->online = 1;
    c->ready = 1;

    lapic_enable();

    asm volatile("mfence" ::: "memory");
    tramp->ready = 1;

    for (;;)
        asm volatile("hlt");
}

static void copy_trampoline(void)
{
    u64 len = (u64)_trampoline_end - (u64)_trampoline_start;
    u8 *dst = (u8 *)TRAMP_PHYS_CODE;
    u8 *src = (u8 *)_trampoline_start;

    for (u64 i = 0; i < len; i++)
        dst[i] = src[i];
}

static void enumerate_cpus(void)
{
    if (!acpi_madt)
        return;

    u8 *ptr = (u8 *)acpi_madt + sizeof(struct madt);
    u32 length = acpi_madt->header.length;
    u32 offset = sizeof(struct madt);

    while (offset + sizeof(struct madt_entry_header) <= length) {
        struct madt_entry_header *entry = (struct madt_entry_header *)ptr;
        if (entry->length == 0)
            break;

        if (entry->type == MADT_TYPE_LOCAL_APIC) {
            struct madt_lapic *la = (struct madt_lapic *)entry;
            if (!(la->flags & 1))
                goto next;

            if (cpu_count >= SMP_MAX_CPUS)
                break;

            struct cpu *c = &cpus[cpu_count];
            c->id = (u8)cpu_count;
            c->apic_id = la->apic_id;
            c->bsp = (la->acpi_processor_id == 0) ? 1 : 0;
            c->online = 0;
            c->ready = 0;

            if (c->bsp)
                bsp_cpu = c;

            klog("smp", "CPU %u: apic_id=%u%s",
                 c->id, c->apic_id, c->bsp ? " (BSP)" : "");
            cpu_count++;
        }

next:
        ptr += entry->length;
        offset += entry->length;
    }
}

static void start_ap(struct cpu *c)
{
    u64 stack_top = (u64)&ap_stacks[c->id] + sizeof(ap_stacks[c->id]);
    u64 cr3 = paging_cr3();

    tramp->stack = stack_top;
    tramp->cr3 = cr3;
    tramp->entry = (u64)ap_entry;
    tramp->cpu_id = c->id;
    tramp->ready = 0;

    asm volatile("mfence" ::: "memory");

    /* INIT IPI: assert, edge-triggered, all-excluding-self */
    lapic_write(LAPIC_ICR_HIGH, 0);
    lapic_write(LAPIC_ICR_LOW, (ICR_DEL_INIT << 8) | (1 << 14) | (ICR_SHORTHAND_ALL_EXC << 18));
    lapic_wait_delivery();
    delay_ms(10);

    /* SIPI: vector = 0x08 -> start at 0x8000 */
    lapic_write(LAPIC_ICR_HIGH, 0);
    lapic_write(LAPIC_ICR_LOW, 0x08 | (ICR_DEL_SIPI << 8) | (1 << 14) | (ICR_SHORTHAND_ALL_EXC << 18));
    lapic_wait_delivery();
    delay_ms(10);

    /* second SIPI per Intel spec */
    lapic_write(LAPIC_ICR_HIGH, 0);
    lapic_write(LAPIC_ICR_LOW, 0x08 | (ICR_DEL_SIPI << 8) | (1 << 14) | (ICR_SHORTHAND_ALL_EXC << 18));
    lapic_wait_delivery();
    delay_ms(10);

    /* poll the ready flag */
    for (u32 i = 0; i < 1000000; i++) {
        if (tramp->ready) {
            c->online = 1;
            c->ready = 1;
            return;
        }
        asm volatile("pause");
    }

    u8 marker = *(volatile u8 *)0x82F0;
    klog("smp", "AP apic_id=%u did not come up (marker=0x%02X)", c->apic_id, marker);
}

void smp_init(void)
{
    cpu_count = 0;
    bsp_cpu = 0;

    enumerate_cpus();

    if (cpu_count == 0) {
        klog("smp", "no CPUs found in MADT");
        return;
    }

    if (cpu_count == 1) {
        klog("smp", "SMP: only BSP detected, no APs to start");
        bsp_cpu->online = 1;
        return;
    }

    lapic_init();

    pmm_reserve(TRAMP_PHYS_CODE, PAGE_SIZE);
    pmm_reserve(TRAMP_PHYS_DATA, PAGE_SIZE);
    copy_trampoline();

    /* verify trampoline was copied correctly */
    {
        u8 *src = (u8 *)_trampoline_start;
        u8 *dst = (u8 *)TRAMP_PHYS_CODE;
        u64 len = (u64)_trampoline_end - (u64)_trampoline_start;
        int ok = 1;
        for (u64 i = 0; i < len; i++) {
            if (src[i] != dst[i]) {
                klog("smp", "trampoline verify FAIL at offset 0x%llx: got 0x%02X expected 0x%02X",
                     i, dst[i], src[i]);
                ok = 0;
                break;
            }
        }
        if (ok)
            klog("smp", "trampoline verified OK (%llu bytes)", len);
    }

    /* clear debug marker area */
    *(volatile u32 *)0x82F0 = 0;

    klog("smp", "starting %u application processor(s)...", cpu_count - 1);

    for (u32 i = 0; i < cpu_count; i++) {
        struct cpu *c = &cpus[i];
        if (c->bsp) {
            c->online = 1;
            c->ready = 1;
            continue;
        }
        start_ap(c);
    }

    ioapic_init();

    u32 online = 0;
    for (u32 i = 0; i < cpu_count; i++)
        if (cpus[i].online)
            online++;

    klog("smp", "%u CPU(s) online", online);
}

u32 smp_get_cpu_count(void)
{
    return cpu_count;
}

struct cpu *smp_get_cpu(u8 id)
{
    if (id >= cpu_count)
        return 0;
    return &cpus[id];
}

struct cpu *smp_current_cpu(void)
{
    return bsp_cpu;
}
