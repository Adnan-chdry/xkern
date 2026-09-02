#include "smp.h"
#include "acpi/acpi.h"
#include "klog.h"
#include "paging.h"

static volatile u8 *ioapic_base;
static u32 ioapic_gsi_base;

static inline void mmio_write32(volatile u8 *base, u32 offset, u32 val)
{
    *(volatile u32 *)(base + offset) = val;
}

static inline u32 mmio_read32(volatile u8 *base, u32 offset)
{
    return *(volatile u32 *)(base + offset);
}

u32 ioapic_read(u32 reg)
{
    mmio_write32(ioapic_base, IOAPIC_REGSEL, reg);
    return mmio_read32(ioapic_base, IOAPIC_WIN);
}

void ioapic_write(u32 reg, u32 val)
{
    mmio_write32(ioapic_base, IOAPIC_REGSEL, reg);
    mmio_write32(ioapic_base, IOAPIC_WIN, val);
}

void ioapic_set_redirect(u8 vector, u8 dest_apic_id, int masked)
{
    u32 low = vector | (0 << 8) | (0 << 11) | (1 << 13) | (1 << 15);
    if (masked)
        low |= (1 << 16);

    u32 high = (u32)dest_apic_id << 24;

    for (u32 i = 0; i < 24; i++) {
        u32 reg = IOAPIC_REDIR_BASE + i * 2;
        ioapic_write(reg, low);
        ioapic_write(reg + 1, high);
    }
}

void ioapic_mask_all(void)
{
    u32 max = (ioapic_read(IOAPIC_VER_REG) >> 16) & 0xFF;
    if (max == 0)
        max = 23;

    for (u32 i = 0; i <= max; i++) {
        u32 reg = IOAPIC_REDIR_BASE + i * 2;
        ioapic_write(reg, (1 << 16));
        ioapic_write(reg + 1, 0);
    }
}

void ioapic_init(void)
{
    ioapic_base = 0;
    ioapic_gsi_base = 0;

    if (!acpi_madt) {
        klog("smp", "no MADT, skipping I/O APIC init");
        return;
    }

    u8 *ptr = (u8 *)acpi_madt + sizeof(struct madt);
    u32 length = acpi_madt->header.length;
    u32 offset = sizeof(struct madt);

    while (offset + sizeof(struct madt_entry_header) <= length) {
        struct madt_entry_header *entry = (struct madt_entry_header *)ptr;
        if (entry->length == 0)
            break;
        if (entry->type == MADT_TYPE_IO_APIC) {
            struct madt_ioapic *io = (struct madt_ioapic *)entry;
            ioapic_base = (volatile u8 *)(u64)io->ioapic_addr;
            ioapic_gsi_base = io->gsi_base;
            klog("smp", "I/O APIC id=%u addr=0x%x gsi_base=%u",
                 io->ioapic_id, io->ioapic_addr, io->gsi_base);
            break;
        }
        ptr += entry->length;
        offset += entry->length;
    }

    if (!ioapic_base) {
        klog("smp", "no I/O APIC found in MADT");
        return;
    }

    paging_map_region((u64)ioapic_base, (u64)ioapic_base,
                      0x1000, PAGE_CACHE_WC);

    ioapic_mask_all();

    u32 ver = ioapic_read(IOAPIC_VER_REG);
    klog("smp", "I/O APIC version=0x%x max_redir=%u",
         ver & 0xFF, (ver >> 16) & 0xFF);
}
