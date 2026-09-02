#include "acpi.h"
#include "klog.h"
#include "stdio.h"
#include "paging.h"
#include "pmm.h"

int acpi_parse_rsdt(void)
{
    if (!acpi_rsdp || acpi_rsdp->revision < 2) {
        return 0;
    }
    return 1;
}

int acpi_parse_xsdt(void)
{
    if (!acpi_rsdp || acpi_rsdp->revision < 2) {
        return 0;
    }

    struct rsdp_descriptor_v2 *rsdp2 = (struct rsdp_descriptor_v2 *)acpi_rsdp;

    if (rsdp2->xsdt_address == 0) {
        klog("acpi", "XSDT address is zero, falling back to RSDT");
        acpi_rsdt = (struct acpi_sdt_header *)(uintptr_t)acpi_rsdp->rsdt_address;
        if (!acpi_rsdt) {
            klog("acpi", "RSDT address is null");
            return 0;
        }
        acpi_map_phys((u64)(uintptr_t)acpi_rsdt, PAGE_SIZE);
        if (acpi_checksum((const u8 *)acpi_rsdt, acpi_rsdt->length) != 0) {
            klog("acpi", "RSDT checksum failed");
            return 0;
        }
        return 1;
    }

    acpi_xsdt = (struct acpi_sdt_header *)(uintptr_t)rsdp2->xsdt_address;
    if (!acpi_xsdt) {
        klog("acpi", "XSDT address is null");
        return 0;
    }
    acpi_map_phys((u64)(uintptr_t)acpi_xsdt, PAGE_SIZE);
    if (acpi_checksum((const u8 *)acpi_xsdt, acpi_xsdt->length) != 0) {
        klog("acpi", "XSDT checksum failed");
        acpi_xsdt = 0;
        return 0;
    }

    klog("acpi", "XSDT found at 0x%llx",
         (unsigned long long)(uintptr_t)acpi_xsdt);
    return 1;
}
