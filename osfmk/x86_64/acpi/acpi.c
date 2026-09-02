#include "acpi.h"
#include "klog.h"
#include "stdio.h"
#include "paging.h"
#include "pmm.h"
#include <stdint.h>

struct rsdp_descriptor *acpi_rsdp = 0;
struct acpi_sdt_header *acpi_rsdt = 0;
struct acpi_sdt_header *acpi_xsdt = 0;
struct madt *acpi_madt = 0;
struct fadt *acpi_fadt = 0;

u8 acpi_checksum(const u8 *data, u32 length)
{
    u8 sum = 0;
    for (u32 i = 0; i < length; i++) {
        sum += data[i];
    }
    return sum;
}

void acpi_map_phys(u64 phys, u64 size)
{
    u64 cr0;

    if (!phys || !size)
        return;

    asm volatile("movq %%cr0, %0" : "=r"(cr0));
    if (!(cr0 & 0x80000000))
        return;

    paging_map_region(phys, phys, size, PAGE_WRITE);
}

void acpi_print_sdt(struct acpi_sdt_header *sdt)
{
    char sig[5];
    for (int i = 0; i < 4; i++) {
        sig[i] = sdt->signature[i];
    }
    sig[4] = '\0';

    klog("acpi", "SDT: %s @ 0x%llx", sig, (unsigned long long)(uintptr_t)sdt);
    klog("acpi", "  Length: %d", sdt->length);
    klog("acpi", "  Revision: %d", sdt->revision);
    klog("acpi", "  OEM: %.6s", sdt->oem_id);
    klog("acpi", "  OEM Table: %.8s", sdt->oem_table_id);
    klog("acpi", "  OEM Revision: 0x%x", sdt->oem_revision);
    klog("acpi", "  Checksum: %s", acpi_checksum((const u8 *)sdt, sdt->length) == 0 ? "OK" : "FAIL");
}

struct acpi_sdt_header *acpi_find_table(const char *signature)
{
    if (!acpi_rsdt && !acpi_xsdt) {
        return 0;
    }

    if (acpi_xsdt) {
        u32 count = (acpi_xsdt->length - sizeof(struct acpi_sdt_header)) / sizeof(uint64_t);
        uint64_t *entries = (uint64_t *)((u8 *)acpi_xsdt + sizeof(struct acpi_sdt_header));

        for (u32 i = 0; i < count; i++) {
            u64 base = entries[i];
            if (!base)
                continue;

            acpi_map_phys(base, PAGE_SIZE);
            struct acpi_sdt_header *sdt = (struct acpi_sdt_header *)(uintptr_t)base;
            if (sdt->signature[0] == signature[0] && sdt->signature[1] == signature[1] &&
                sdt->signature[2] == signature[2] && sdt->signature[3] == signature[3]) {
                acpi_map_phys(base, sdt->length);
                if (acpi_checksum((const u8 *)sdt, sdt->length) == 0) {
                    return sdt;
                }
            }
        }
    }

    if (acpi_rsdt) {
        u32 count = (acpi_rsdt->length - sizeof(struct acpi_sdt_header)) / sizeof(u32);
        u32 *entries = (u32 *)((u8 *)acpi_rsdt + sizeof(struct acpi_sdt_header));

        for (u32 i = 0; i < count; i++) {
            u32 base = entries[i];
            if (!base)
                continue;

            acpi_map_phys(base, PAGE_SIZE);
            struct acpi_sdt_header *sdt = (struct acpi_sdt_header *)(uintptr_t)base;
            if (sdt->signature[0] == signature[0] && sdt->signature[1] == signature[1] &&
                sdt->signature[2] == signature[2] && sdt->signature[3] == signature[3]) {
                acpi_map_phys(base, sdt->length);
                if (acpi_checksum((const u8 *)sdt, sdt->length) == 0) {
                    return sdt;
                }
            }
        }
    }

    return 0;
}

int acpi_init(void)
{
    klog("acpi", "Initializing ACPI...");

    if (!acpi_rsdp_scan()) {
        klog("acpi", "ACPI not available");
        return 0;
    }

    if (acpi_rsdp->revision >= 2) {
        if (!acpi_parse_xsdt()) {
            klog("acpi", "Failed to parse XSDT");
            return 0;
        }
    } else {
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
    }

    klog("acpi", "System Description Table found");

    struct acpi_sdt_header *madt_sdt = acpi_find_table(ACPI_SIG_MADT);
    if (madt_sdt) {
        acpi_madt = (struct madt *)madt_sdt;
        acpi_print_madt(acpi_madt);
    }

    struct acpi_sdt_header *fadt_sdt = acpi_find_table(ACPI_SIG_FADT);
    if (fadt_sdt) {
        acpi_fadt = (struct fadt *)fadt_sdt;
        acpi_print_fadt(acpi_fadt);
    }

    return 1;
}
