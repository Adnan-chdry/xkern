#include "smp.h"
#include "acpi/acpi.h"
#include "klog.h"
#include "paging.h"

static volatile u32 *lapic_base;

u32 lapic_read(u32 reg)
{
    return *(volatile u32 *)((u8 *)lapic_base + reg);
}

void lapic_write(u32 reg, u32 val)
{
    *(volatile u32 *)((u8 *)lapic_base + reg) = val;
}

void lapic_eoi(void)
{
    lapic_write(LAPIC_EOI, 0);
}

u8 lapic_get_id(void)
{
    return (u8)(lapic_read(LAPIC_ID) >> 24);
}

void lapic_enable(void)
{
    lapic_write(LAPIC_TPR, 0x00);
    lapic_write(LAPIC_SPURIOUS, 0x100 | 0xFF);
    lapic_write(LAPIC_LVT_LINT0, 0x0700);
    lapic_write(LAPIC_LVT_LINT1, 0x0400);
}

void lapic_send_ipi(u32 dest, u32 vector, u32 delivery, u32 shorthand)
{
    lapic_write(LAPIC_ICR_HIGH, dest << 24);
    lapic_write(LAPIC_ICR_LOW, vector | (delivery << 8) | (shorthand << 18));
}

void lapic_init(void)
{
    lapic_base = (volatile u32 *)LAPIC_BASE_DEFAULT;

    if (acpi_madt) {
        u8 *ptr = (u8 *)acpi_madt + sizeof(struct madt);
        u32 length = acpi_madt->header.length;
        u32 offset = sizeof(struct madt);

        while (offset + sizeof(struct madt_entry_header) <= length) {
            struct madt_entry_header *entry = (struct madt_entry_header *)ptr;
            if (entry->length == 0)
                break;
            if (entry->type == MADT_TYPE_LOCAL_APIC_ADDR) {
                struct madt_lapic_addr *la = (struct madt_lapic_addr *)entry;
                lapic_base = (volatile u32 *)(u64)la->lapic_addr;
                klog("smp", "LAPIC MMIO remapped to 0x%lx", (unsigned long)la->lapic_addr);
                break;
            }
            ptr += entry->length;
            offset += entry->length;
        }
    }

    paging_map_region(LAPIC_BASE_DEFAULT, LAPIC_BASE_DEFAULT,
                      0x1000, PAGE_CACHE_WC);

    lapic_enable();
    klog("smp", "LAPIC enabled at 0x%llx (id=%u)",
         (u64)lapic_base, lapic_get_id());
}
