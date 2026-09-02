#ifndef SMBIOS_H
#define SMBIOS_H

#include <stdint.h>
#include <stddef.h>

/*
 * SMBIOS physical memory scan range.
 *
 * SMBIOS 2.x/3.x entry points are normally located in
 * the 0xF0000 - 0xFFFFF BIOS area.
 */
#define SMBIOS_SCAN_START  0x000F0000UL
#define SMBIOS_SCAN_END    0x00100000UL


/* ---------------------------------------------------------
 * SMBIOS structure types
 * --------------------------------------------------------- */

#define SMBIOS_TYPE_BIOS        0
#define SMBIOS_TYPE_SYSTEM      1
#define SMBIOS_TYPE_BASEBOARD   2
#define SMBIOS_TYPE_CHASSIS     3
#define SMBIOS_TYPE_PROCESSOR   4
#define SMBIOS_TYPE_MEMORY      17
#define SMBIOS_TYPE_END         127


/* ---------------------------------------------------------
 * SMBIOS 2.x Entry Point
 * --------------------------------------------------------- */

struct smbios2_entry {
    char     anchor[4];          /* "_SM_" */
    uint8_t  checksum;
    uint8_t  length;
    uint8_t  major;
    uint8_t  minor;
    uint16_t max_structure_size;
    uint8_t  revision;
    uint8_t  formatted_area[5];

    char     dmi_anchor[5];      /* "_DMI_" */
    uint8_t  dmi_checksum;
    uint16_t table_length;
    uint32_t table_address;
    uint16_t structure_count;
    uint8_t  bcd_revision;
} __attribute__((packed));


/* ---------------------------------------------------------
 * SMBIOS 3.x Entry Point
 * --------------------------------------------------------- */

struct smbios3_entry {
    char     anchor[5];          /* "_SM3_" */
    uint8_t  checksum;
    uint8_t  length;
    uint8_t  major;
    uint8_t  minor;
    uint8_t  docrev;
    uint8_t  revision;
    uint8_t  reserved;
    uint32_t table_max_size;
    uint64_t table_address;
} __attribute__((packed));


/* ---------------------------------------------------------
 * SMBIOS Structure Header
 * --------------------------------------------------------- */

struct smbios_header {
    uint8_t  type;
    uint8_t  length;
    uint16_t handle;
} __attribute__((packed));


/* ---------------------------------------------------------
 * Parsed SMBIOS information
 * --------------------------------------------------------- */

struct smbios_info {
    uint64_t table_address;
    uint32_t table_length;

    /*
     * SMBIOS 2.x provides this.
     * SMBIOS 3.x does not.
     */
    uint16_t structure_count;

    uint8_t major;
    uint8_t minor;

    /*
     * 0 = SMBIOS 2.x
     * 1 = SMBIOS 3.x
     */
    int version3;
};


/* ---------------------------------------------------------
 * SMBIOS initialization
 * --------------------------------------------------------- */

/*
 * Locate and validate the SMBIOS entry point.
 *
 * Returns:
 *   1 = SMBIOS found
 *   0 = SMBIOS not found
 */
int smbios_init(struct smbios_info *info);


/* ---------------------------------------------------------
 * SMBIOS string handling
 * --------------------------------------------------------- */

/*
 * Get a string from an SMBIOS structure.
 *
 * index:
 *   0 = no string
 *   1+ = SMBIOS string index
 */
const char *smbios_get_string(struct smbios_header *h,
                              uint8_t index);


/* ---------------------------------------------------------
 * SMBIOS structure traversal
 * --------------------------------------------------------- */

/*
 * Find the first SMBIOS structure of the specified type.
 *
 * Example:
 *
 *   smbios_find_type(&info, SMBIOS_TYPE_SYSTEM);
 */
struct smbios_header *
smbios_find_type(const struct smbios_info *info,
                 uint8_t type);


/* ---------------------------------------------------------
 * Printing helpers
 * --------------------------------------------------------- */

/*
 * Print SMBIOS Type 0:
 * BIOS Information.
 */
void smbios_print_bios(const struct smbios_info *info);


/*
 * Print SMBIOS Type 1:
 * System Information.
 */
void smbios_print_system(const struct smbios_info *info);


/*
 * Print general SMBIOS information and
 * BIOS/System information.
 */
void smbios_print(const struct smbios_info *info);


/*
 * Test SMBIOS initialization and print information.
 *
 * Intended to be called from your kernel initialization code.
 */
void smbios_test(void);

#endif /* SMBIOS_H */