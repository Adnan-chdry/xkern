#ifndef ACPI_H
#define ACPI_H

#include "types.h"
#include <stdint.h>

#define ACPI_SIG_RSDP   "RSD PTR "
#define ACPI_SIG_RSDT   "RSDT"
#define ACPI_SIG_XSDT   "XSDT"
#define ACPI_SIG_MADT   "APIC"
#define ACPI_SIG_FADT   "FACP"
#define ACPI_SIG_DSDT   "DSDT"
#define ACPI_SIG_SSDT   "SSDT"
#define ACPI_SIG_MCFG   "MCFG"

struct rsdp_descriptor {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_address;
} __attribute__((packed));

struct rsdp_descriptor_v2 {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_address;

    u32 length;
    uint64_t xsdt_address;
    u8 extended_checksum;
    u8 reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;
} __attribute__((packed));

struct rsdt {
    struct acpi_sdt_header header;
    u32 entries[];
} __attribute__((packed));

struct xsdt {
    struct acpi_sdt_header header;
    uint64_t entries[];
} __attribute__((packed));

struct madt_entry_header {
    u8 type;
    u8 length;
} __attribute__((packed));

#define MADT_TYPE_LOCAL_APIC      0
#define MADT_TYPE_IO_APIC         1
#define MADT_TYPE_INT_SRC_OVERRIDE 2
#define MADT_TYPE_NMI_SRC         3
#define MADT_TYPE_LOCAL_APIC_NMI  4
#define MADT_TYPE_LOCAL_APIC_ADDR 5
#define MADT_TYPE_LOCAL_X2APIC    9

struct madt_lapic {
    struct madt_entry_header header;
    u8 acpi_processor_id;
    u8 apic_id;
    u32 flags;
} __attribute__((packed));

struct madt_ioapic {
    struct madt_entry_header header;
    u8 ioapic_id;
    u8 reserved;
    u32 ioapic_addr;
    u32 gsi_base;
} __attribute__((packed));

struct madt_int_src_override {
    struct madt_entry_header header;
    u8 bus;
    u8 source;
    u32 gsi;
    u16 flags;
} __attribute__((packed));

struct madt_lapic_addr {
    struct madt_entry_header header;
    u16 reserved;
    uint64_t lapic_addr;
} __attribute__((packed));

struct madt {
    struct acpi_sdt_header header;
    u32 lapic_addr;
    u32 flags;
    struct madt_entry_header entries[];
} __attribute__((packed));

struct fadt {
    struct acpi_sdt_header header;
    u32 firmware_ctrl;
    u32 dsdt;
    u8 reserved;
    u8 preferred_pm_profile;
    u16 sci_int;
    u32 smi_cmd;
    u8 acpi_enable;
    u8 acpi_disable;
    u8 s4bios_req;
    u8 pstate_cnt;
    u32 pm1a_evt_blk;
    u32 pm1b_evt_blk;
    u32 pm1a_cnt_blk;
    u32 pm1b_cnt_blk;
    u32 pm2_cnt_blk;
    u32 pm_tmr_blk;
    u32 gpe0_blk;
    u32 gpe1_blk;
    u8 pm1_evt_len;
    u8 pm1_cnt_len;
    u8 pm2_cnt_len;
    u8 pm_tmr_len;
    u8 gpe0_blk_len;
    u8 gpe1_blk_len;
    u8 gpe1_base;
    u8 cst_cnt;
    u16 p_lvl2_lat;
    u16 p_lvl3_lat;
    u16 flush_size;
    u16 flush_stride;
    u8 duty_offset;
    u8 duty_width;
    u8 day_alrm;
    u8 mon_alrm;
    u8 century;
    u16 iapc_boot_arch;
    u8 reserved2;
    u32 flags;
    u32 reset_reg;
    u8 reset_value;
    u8 reserved3[3];
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    u32 x_pm1a_evt_blk;
    u32 x_pm1b_evt_blk;
    u32 x_pm1a_cnt_blk;
    u32 x_pm1b_cnt_blk;
    u32 x_pm2_cnt_blk;
    u32 x_pm_tmr_blk;
    u32 x_gpe0_blk;
    u32 x_gpe1_blk;
} __attribute__((packed));

extern struct rsdp_descriptor *acpi_rsdp;
extern struct acpi_sdt_header *acpi_rsdt;
extern struct acpi_sdt_header *acpi_xsdt;
extern struct madt *acpi_madt;
extern struct fadt *acpi_fadt;

u8 acpi_checksum(const u8 *data, u32 length);
void acpi_map_phys(u64 phys, u64 size);
int acpi_rsdp_scan(void);
int acpi_parse_rsdt(void);
int acpi_parse_xsdt(void);
int acpi_init(void);

struct acpi_sdt_header *acpi_find_table(const char *signature);

void acpi_print_rsdp(struct rsdp_descriptor *rsdp);
void acpi_print_sdt(struct acpi_sdt_header *sdt);
void acpi_print_madt(struct madt *madt);
void acpi_print_fadt(struct fadt *fadt);

#endif
