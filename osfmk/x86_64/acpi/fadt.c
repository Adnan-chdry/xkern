#include "acpi.h"
#include "klog.h"
#include "stdio.h"

void acpi_print_fadt(struct fadt *fadt)
{
    klog("acpi", "FADT @ 0x%x", (unsigned int)fadt);
    klog("acpi", "  SciInt: %d", fadt->sci_int);
    klog("acpi", "  SmiCmd: 0x%x", fadt->smi_cmd);
    klog("acpi", "  AcpiEnable: 0x%x", fadt->acpi_enable);
    klog("acpi", "  AcpiDisable: 0x%x", fadt->acpi_disable);
    klog("acpi", "  PM1aEvtBlk: 0x%x", fadt->pm1a_evt_blk);
    klog("acpi", "  PM1bEvtBlk: 0x%x", fadt->pm1b_evt_blk);
    klog("acpi", "  PM1aCntBlk: 0x%x", fadt->pm1a_cnt_blk);
    klog("acpi", "  PM1bCntBlk: 0x%x", fadt->pm1b_cnt_blk);
    klog("acpi", "  PM_TmrBlk: 0x%x", fadt->pm_tmr_blk);
    klog("acpi", "  GPE0Blk: 0x%x", fadt->gpe0_blk);
    klog("acpi", "  GPE1Blk: 0x%x", fadt->gpe1_blk);
    klog("acpi", "  PM1EvtLen: %d", fadt->pm1_evt_len);
    klog("acpi", "  PM1CntLen: %d", fadt->pm1_cnt_len);
    klog("acpi", "  Flags: 0x%x", fadt->flags);
    klog("acpi", "  Century: %d", fadt->century);
    klog("acpi", "  IAPC Boot Arch: 0x%x", fadt->iapc_boot_arch);
    klog("acpi", "  DSDT: 0x%x", fadt->dsdt);
}
