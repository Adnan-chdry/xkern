#include <stdint.h>
#include <stddef.h>
#include "klibc.h"
#include "klog.h"



/* ---------------------------------------------------------
 * Configuration
 * --------------------------------------------------------- */

#define SMBIOS_SCAN_START  0x000F0000UL
#define SMBIOS_SCAN_END    0x00100000UL

#define SMBIOS_TYPE_BIOS       0
#define SMBIOS_TYPE_SYSTEM     1
#define SMBIOS_TYPE_BASEBOARD  2
#define SMBIOS_TYPE_PROCESSOR  4
#define SMBIOS_TYPE_MEMORY     17


/* ---------------------------------------------------------
 * SMBIOS Entry Point Structures
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
    uint16_t structure_count;

    uint8_t major;
    uint8_t minor;

    int version3;
};


/* ---------------------------------------------------------
 * Utility functions
 * --------------------------------------------------------- */

static int smbios_memcmp(const void *a, const char *b, size_t len)
{
    const uint8_t *p = (const uint8_t *)a;

    for (size_t i = 0; i < len; i++) {
        if (p[i] != (uint8_t)b[i])
            return 0;
    }

    return 1;
}


static int smbios_checksum(const void *ptr, size_t len)
{
    const uint8_t *p = (const uint8_t *)ptr;
    uint8_t sum = 0;

    for (size_t i = 0; i < len; i++)
        sum += p[i];

    return sum == 0;
}


/* ---------------------------------------------------------
 * Find SMBIOS entry point
 * --------------------------------------------------------- */

int smbios_init(struct smbios_info *info)
{
    if (!info)
        return 0;

    /*
     * SMBIOS entry points are aligned on 16-byte boundaries.
     */
    for (uintptr_t addr = SMBIOS_SCAN_START;
         addr < SMBIOS_SCAN_END;
         addr += 16) {

        /*
         * Check SMBIOS 3.x first.
         */
        struct smbios3_entry *e3 =
            (struct smbios3_entry *)addr;

        if (smbios_memcmp(e3->anchor, "_SM3_", 5)) {

            /*
             * SMBIOS 3 entry point length is normally 0x18.
             */
            if (e3->length >= sizeof(struct smbios3_entry) &&
                e3->length <= 0x40 &&
                addr + e3->length <= SMBIOS_SCAN_END &&
                smbios_checksum(e3, e3->length)) {

                info->table_address  = e3->table_address;
                info->table_length   = e3->table_max_size;
                info->structure_count = 0;

                info->major = e3->major;
                info->minor = e3->minor;

                info->version3 = 1;

                return 1;
            }
        }


        /*
         * Check SMBIOS 2.x.
         */
        struct smbios2_entry *e2 =
            (struct smbios2_entry *)addr;

        if (smbios_memcmp(e2->anchor, "_SM_", 4)) {

            /*
             * The SMBIOS 2 entry-point structure is at least
             * 0x1F bytes.
             */
            if (e2->length >= sizeof(struct smbios2_entry) &&
                e2->length <= 0x40 &&
                addr + e2->length <= SMBIOS_SCAN_END &&
                smbios_checksum(e2, e2->length) &&

                /*
                 * DMI anchor must also be valid.
                 */
                smbios_memcmp(e2->dmi_anchor, "_DMI_", 5) &&

                /*
                 * DMI portion is 15 bytes.
                 */
                smbios_checksum(e2->dmi_anchor, 15)) {

                info->table_address =
                    (uint64_t)e2->table_address;

                info->table_length =
                    e2->table_length;

                info->structure_count =
                    e2->structure_count;

                info->major = e2->major;
                info->minor = e2->minor;

                info->version3 = 0;

                return 1;
            }
        }
    }

    return 0;
}


/* ---------------------------------------------------------
 * Get string from an SMBIOS structure
 * --------------------------------------------------------- */

const char *smbios_get_string(struct smbios_header *h,
                              uint8_t index)
{
    if (!h || index == 0)
        return NULL;

    /*
     * Strings begin immediately after the formatted area.
     */
    uint8_t *p =
        (uint8_t *)h + h->length;

    for (uint8_t current = 1;
         current < index;
         current++) {

        /*
         * Empty string set / malformed structure.
         */
        if (*p == 0)
            return NULL;

        /*
         * Skip current string.
         */
        while (*p)
            p++;

        /*
         * Move to next string.
         */
        p++;
    }

    /*
     * Index points at this string.
     */
    if (*p == 0)
        return NULL;

    return (const char *)p;
}


/* ---------------------------------------------------------
 * Find the next SMBIOS structure
 * --------------------------------------------------------- */

static struct smbios_header *
smbios_next(struct smbios_header *h)
{
    uint8_t *p;

    if (!h)
        return NULL;

    /*
     * Skip formatted section.
     */
    p = (uint8_t *)h + h->length;

    /*
     * Find double NUL terminating the string-set.
     */
    while (!(p[0] == 0 && p[1] == 0))
        p++;

    /*
     * Skip the double NUL.
     */
    p += 2;

    return (struct smbios_header *)p;
}


/* ---------------------------------------------------------
 * Find structure by type
 * --------------------------------------------------------- */

struct smbios_header *
smbios_find_type(const struct smbios_info *info,
                 uint8_t type)
{
    if (!info)
        return NULL;

    uintptr_t start =
        (uintptr_t)info->table_address;

    uintptr_t end =
        start + info->table_length;

    uintptr_t p = start;

    uint16_t count = 0;

    while (p + sizeof(struct smbios_header) <= end) {

        struct smbios_header *h =
            (struct smbios_header *)p;

        /*
         * Protect against corrupt structures.
         */
        if (h->length < sizeof(struct smbios_header))
            return NULL;

        if (p + h->length > end)
            return NULL;

        /*
         * SMBIOS Type 127 = End-of-table.
         */
        if (h->type == 127)
            break;

        if (h->type == type)
            return h;

        p = (uintptr_t)smbios_next(h);

        if (p <= (uintptr_t)h || p > end)
            return NULL;

        /*
         * SMBIOS 2.x provides structure_count.
         */
        if (!info->version3) {
            count++;

            if (count >= info->structure_count)
                break;
        }
    }

    return NULL;
}


/* ---------------------------------------------------------
 * Print UUID
 * --------------------------------------------------------- */

static void smbios_print_uuid(struct smbios_header *h)
{
    /*
     * SMBIOS Type 1 UUID starts at offset 8.
     *
     * Header:
     *
     * +0  Type
     * +1  Length
     * +2  Handle
     *
     * Type 1:
     * +4  Manufacturer
     * +5  Product
     * +6  Version
     * +7  Serial
     * +8  UUID[16]
     */
    if (!h || h->length < 24) {
        klog("smbios", "UUID         : N/A");
        return;
    }

    uint8_t *uuid =
        (uint8_t *)h + 8;

    /*
     * SMBIOS stores UUID bytes in a format where the first
     * three fields are little-endian for the conventional
     * UUID representation.
     */
    char uuid_str[40];
    klibc.snprintf(uuid_str, sizeof(uuid_str),
           "%02x%02x%02x%02x-"
           "%02x%02x-"
           "%02x%02x-"
           "%02x%02x-"
           "%02x%02x%02x%02x%02x%02x",

           uuid[3], uuid[2], uuid[1], uuid[0],
           uuid[5], uuid[4],
           uuid[7], uuid[6],
           uuid[8], uuid[9],
           uuid[10], uuid[11],
           uuid[12], uuid[13],
           uuid[14], uuid[15]);

    klog("smbios", "UUID         : %s", uuid_str);
}


/* ---------------------------------------------------------
 * Print SMBIOS Type 1 - System Information
 * --------------------------------------------------------- */

void smbios_print_system(const struct smbios_info *info)
{
    struct smbios_header *h;

    const char *manufacturer;
    const char *product;
    const char *version;
    const char *serial;

    h = smbios_find_type(info, SMBIOS_TYPE_SYSTEM);

    if (!h) {
        klog("smbios", "System Information not found");
        return;
    }

    /*
     * Type 1 formatted area:
     *
     * offset 4 = Manufacturer string index
     * offset 5 = Product Name string index
     * offset 6 = Version string index
     * offset 7 = Serial Number string index
     */
    if (h->length < 8) {
        klog_lvl(KLOG_WARNING, "smbios", "Invalid Type 1 structure");
        return;
    }

    manufacturer =
        smbios_get_string(h, *((uint8_t *)h + 4));

    product =
        smbios_get_string(h, *((uint8_t *)h + 5));

    version =
        smbios_get_string(h, *((uint8_t *)h + 6));

    serial =
        smbios_get_string(h, *((uint8_t *)h + 7));


    klog("smbios", "System Information:");
    klog("smbios", "Manufacturer : %s",
           manufacturer ? manufacturer : "N/A");

    klog("smbios", "Product      : %s",
           product ? product : "N/A");

    klog("smbios", "Version      : %s",
           version ? version : "N/A");

    klog("smbios", "Serial       : %s",
           serial ? serial : "N/A");

    smbios_print_uuid(h);

    klog("smbios", " ");
}


/* ---------------------------------------------------------
 * Print SMBIOS Type 0 - BIOS Information
 * --------------------------------------------------------- */

void smbios_print_bios(const struct smbios_info *info)
{
    struct smbios_header *h;

    const char *vendor;
    const char *version;
    const char *release_date;

    h = smbios_find_type(info, SMBIOS_TYPE_BIOS);

    if (!h) {
        klog("smbios", "BIOS Information not found");
        return;
    }

    if (h->length < 8) {
        klog_lvl(KLOG_WARNING, "smbios", "Invalid Type 0 structure");
        return;
    }

    /*
     * Type 0:
     *
     * offset 4 = Vendor
     * offset 5 = BIOS Version
     * offset 6 = BIOS Starting Address Segment
     * offset 7 = BIOS Release Date
     */
    vendor =
        smbios_get_string(h, *((uint8_t *)h + 4));

    version =
        smbios_get_string(h, *((uint8_t *)h + 5));

    release_date =
        smbios_get_string(h, *((uint8_t *)h + 8));

    klog("smbios", "BIOS Information:");

    klog("smbios", "Vendor       : %s",
           vendor ? vendor : "N/A");

    klog("smbios", "Version      : %s",
           version ? version : "N/A");

    klog("smbios", "Release Date : %s",
           release_date ? release_date : "N/A");

    klog("smbios", " ");
}


/* ---------------------------------------------------------
 * Print complete SMBIOS summary
 * --------------------------------------------------------- */

void smbios_print(const struct smbios_info *info)
{
    if (!info)
        return;

    klog("smbios", "SMBIOS INFO");

    klog("smbios", "SMBIOS Version : %u.%u",
           info->major,
           info->minor);

    klog("smbios", "SMBIOS Type     : %s",
           info->version3 ? "3.x" : "2.x");

    klog("smbios", "Table Address   : 0x%llx",
           (unsigned long long)info->table_address);

    klog("smbios", "Table Length    : %u bytes",
           info->table_length);

    if (!info->version3) {
        klog("smbios", "Structures      : %u",
               info->structure_count);
    }

    klog("smbios", " ");

    smbios_print_bios(info);
    smbios_print_system(info);
}


/* ---------------------------------------------------------
 * Kernel entry point
 * --------------------------------------------------------- */

void smbios_test(void)
{
    struct smbios_info info;

    klog("smbios", "Searching for SMBIOS...");

    if (!smbios_init(&info)) {
        klog_lvl(KLOG_ERR, "smbios", "SMBIOS not found.");
        return;
    }

    klog("smbios", "SMBIOS detected.");

    smbios_print(&info);
}