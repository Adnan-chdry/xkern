#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

#include <stdint.h>
#include "types.h"
#include "multiboot.h"

/*
 * Raw Multiboot 2 wire format (see GNU multiboot2 specification).
 * The information structure is a sequence of tags:
 *
 *   u32 total_size
 *   u32 reserved (0)
 *   tags...
 *   end tag
 *
 * Each tag: u32 type, u32 size (size includes the 8 byte header),
 * followed by type specific fields, padded to 8 byte alignment.
 */

#define MB2_BOOTLOADER_MAGIC  0x36D76289u

/* tag types */
#define MB2_TAG_END            0
#define MB2_TAG_CMDLINE        1
#define MB2_TAG_BOOTLOADER     2
#define MB2_TAG_MODULE         3
#define MB2_TAG_BASIC_MEMINFO  4
#define MB2_TAG_BOOTDEV        5
#define MB2_TAG_MMAP           6
#define MB2_TAG_FRAMEBUFFER    8
#define MB2_TAG_ACPI_OLD       14
#define MB2_TAG_ACPI_NEW       15

struct mb2_tag {
    uint32_t type;
    uint32_t size;
};

struct mb2_tag_cmdline {
    uint32_t type;
    uint32_t size;
    char string[];
};

struct mb2_tag_bootloader {
    uint32_t type;
    uint32_t size;
    char string[];
};

struct mb2_tag_module {
    uint32_t type;
    uint32_t size;
    /* NB: Multiboot 2 uses 32-bit module address fields */
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[];
};

struct mb2_tag_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
};

struct mb2_mmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

struct mb2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct mb2_mmap_entry entries[];
};

struct mb2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[];      /* palette or channel masks by type */
} __attribute__((packed));

/*
 * Parse the raw Multiboot 2 information structure at mbi_phys (physical,
 * identity mapped) and fill the normalized struct multiboot_info.
 */
void multiboot2_parse(u64 mbi_phys, struct multiboot_info *out);

/* physical address of the MBI passed by the bootloader (for hand-off) */
u64 multiboot2_phys(void);

#endif
