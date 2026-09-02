#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "types.h"

/*
 * xkern boots via Multiboot 2.  The raw tag list is parsed once at boot
 * (pexpert/x86_64/multiboot2.c) into this normalized structure, which is
 * what the rest of the kernel consumes.  Flag bit values are kept
 * compatible with the old internal flags.
 */
#define MULTIBOOT_MEM_INFO        (1 << 0)
#define MULTIBOOT_BOOTDEV         (1 << 1)
#define MULTIBOOT_CMDLINE         (1 << 2)
#define MULTIBOOT_MODS_REQ        (1 << 3)
#define MULTIBOOT_AOUT_SYMS       (1 << 4)
#define MULTIBOOT_ELF_SECTIONS    (1 << 5)
#define MULTIBOOT_MMAP            (1 << 6)
#define MULTIBOOT_DRIVES          (1 << 7)
#define MULTIBOOT_CONFIG          (1 << 8)
#define MULTIBOOT_BOOTLOADER_NAME (1 << 9)
#define MULTIBOOT_APM             (1 << 10)
#define MULTIBOOT_VBE_INFO        (1 << 11)
#define MULTIBOOT_FRAMEBUFFER     (1 << 12)

/*
 * Multiboot memory types (identical numbering in the MB2 mmap tag).
 */
#define MULTIBOOT_MEMORY_AVAILABLE        1
#define MULTIBOOT_MEMORY_RESERVED         2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS              4
#define MULTIBOOT_MEMORY_BADRAM           5

/* Multiboot 2 framebuffer types */
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED  0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB      1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT 2

/*
 * Boot module (initramfs cpio archive etc).  The parser copies each
 * MB2 module tag into one of these.
 */
struct multiboot_module {
    u64  mod_start;
    u64  mod_end;
    char string[64];
};

/*
 * Normalized memory map entry (from the MB2 mmap tag: native u64 fields).
 */
struct boot_mem_map_entry {
    u64 base;
    u64 length;
    u32 type;
};

struct multiboot_info {
    u32 flags;

    /* basic meminfo tag */
    u32 mem_lower;
    u32 mem_upper;

    /* strings, copied by the parser into kernel memory */
    const char *cmdline;
    const char *boot_loader_name;

    /* modules */
    u32 mods_count;
    struct multiboot_module *mods_addr;

    /* memory map */
    u32 mmap_count;
    struct boot_mem_map_entry *mmap_addr;

    /* framebuffer tag */
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;

    u8 framebuffer_bpp;
    u8 framebuffer_type;

    u8 framebuffer_red_field_position;
    u8 framebuffer_red_mask_size;
    u8 framebuffer_green_field_position;
    u8 framebuffer_green_mask_size;
    u8 framebuffer_blue_field_position;
    u8 framebuffer_blue_mask_size;
};

#endif
