#include "acpi.h"
#include "io.h"
#include "klog.h"
#include "paging.h"
#include "pmm.h"

extern struct rsdp_descriptor *acpi_rsdp;
extern struct acpi_sdt_header *acpi_rsdt;
extern struct acpi_sdt_header *acpi_xsdt;
extern struct madt *acpi_madt;
extern struct fadt *acpi_fadt;
extern u8 acpi_checksum(const u8 *data, u32 length);

void acpi_print_rsdp(struct rsdp_descriptor *rsdp)
{
    klog("acpi", "RSDP @ 0x%x", (unsigned int)rsdp);
    klog("acpi", "  OEM: %.6s", rsdp->oem_id);
    klog("acpi", "  Revision: %d", rsdp->revision);
    klog("acpi", "  RSDT: 0x%x", rsdp->rsdt_address);
    if (rsdp->revision >= 2) {
        struct rsdp_descriptor_v2 *rsdp2 = (struct rsdp_descriptor_v2 *)rsdp;
        klog("acpi", "  Length: %d", rsdp2->length);
        klog("acpi", "  XSDT: 0x%llx", rsdp2->xsdt_address);
    }
}

int acpi_rsdp_scan(void)
{
    u8 *ebda = (u8 *)(0x40E);
    u32 ebda_base = (*((u16 *)ebda)) << 4;

    klog("acpi", "Scanning for RSDP...");
    klog("acpi", "  EBDA base: 0x%x", ebda_base);

    u32 search_start = ebda_base;
    u32 search_end = ebda_base + 1024;

    if (search_start == 0 || search_end < search_start) {
        search_start = 0x000E0000;
        search_end = 0x000FFFFF;
    }

    acpi_map_phys(search_start, 1024);

    for (u32 addr = search_start; addr < search_end; addr += 16) {
        struct rsdp_descriptor *rsdp = (struct rsdp_descriptor *)addr;

        if (rsdp->signature[0] == 'R' && rsdp->signature[1] == 'S' &&
            rsdp->signature[2] == 'D' && rsdp->signature[3] == ' ' &&
            rsdp->signature[4] == 'P' && rsdp->signature[5] == 'T' &&
            rsdp->signature[6] == 'R' && rsdp->signature[7] == ' ') {

            if (acpi_checksum((const u8 *)rsdp, sizeof(struct rsdp_descriptor)) == 0) {
                acpi_rsdp = rsdp;
                acpi_print_rsdp(rsdp);
                return 1;
            }
        }
    }

    klog("acpi", "RSDP not found in EBDA");
    search_start = 0x000E0000;
    search_end = 0x000FFFFF;
    acpi_map_phys(search_start, 0x00020000);
    for (u32 addr = search_start; addr < search_end; addr += 16) {
        struct rsdp_descriptor *rsdp = (struct rsdp_descriptor *)addr;

        if (rsdp->signature[0] == 'R' && rsdp->signature[1] == 'S' &&
            rsdp->signature[2] == 'D' && rsdp->signature[3] == ' ' &&
            rsdp->signature[4] == 'P' && rsdp->signature[5] == 'T' &&
            rsdp->signature[6] == 'R' && rsdp->signature[7] == ' ') {

            if (acpi_checksum((const u8 *)rsdp, sizeof(struct rsdp_descriptor)) == 0) {
                acpi_rsdp = rsdp;
                acpi_print_rsdp(rsdp);
                return 1;
            }
        }
    }

    klog("acpi", "RSDP not found in BIOS ROM");
    return 0;
}
