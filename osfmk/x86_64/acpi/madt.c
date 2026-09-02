#include "acpi.h"
#include "klog.h"
#include "stdio.h"

void acpi_print_madt(struct madt *madt)
{
    klog("acpi", "MADT @ 0x%x", (unsigned int)madt);
    klog("acpi", "  LAPIC addr: 0x%x", madt->lapic_addr);
    klog("acpi", "  Flags: 0x%x", madt->flags);

    u32 length = madt->header.length;
    u8 *ptr = (u8 *)madt + sizeof(struct acpi_sdt_header);
    u32 offset = sizeof(struct acpi_sdt_header);

    while (offset + sizeof(struct madt_entry_header) <= length) {
        struct madt_entry_header *entry = (struct madt_entry_header *)ptr;

        if (entry->length == 0) {
            break;
        }

        switch (entry->type) {
            case MADT_TYPE_LOCAL_APIC: {
                struct madt_lapic *lapic = (struct madt_lapic *)entry;
                klog("acpi", "  LAPIC: id=%d proc=%d flags=0x%x",
                     lapic->apic_id, lapic->acpi_processor_id, lapic->flags);
                break;
            }
            case MADT_TYPE_IO_APIC: {
                struct madt_ioapic *ioapic = (struct madt_ioapic *)entry;
                klog("acpi", "  IOAPIC: id=%d addr=0x%x gsi_base=%d",
                     ioapic->ioapic_id, ioapic->ioapic_addr, ioapic->gsi_base);
                break;
            }
            case MADT_TYPE_INT_SRC_OVERRIDE: {
                struct madt_int_src_override *iso = (struct madt_int_src_override *)entry;
                klog("acpi", "  INT_SRC_OVERRIDE: bus=%d source=%d gsi=%d flags=0x%x",
                     iso->bus, iso->source, iso->gsi, iso->flags);
                break;
            }
            case MADT_TYPE_LOCAL_APIC_ADDR: {
                struct madt_lapic_addr *lapic_addr = (struct madt_lapic_addr *)entry;
                klog("acpi", "  LAPIC_ADDR: 0x%llx", lapic_addr->lapic_addr);
                break;
            }
            case MADT_TYPE_LOCAL_X2APIC: {
                klog("acpi", "  X2APIC entry (len=%d)", entry->length);
                break;
            }
            default:
                klog("acpi", "  Unknown MADT entry type=%d len=%d", entry->type, entry->length);
                break;
        }

        ptr += entry->length;
        offset += entry->length;
    }
}
