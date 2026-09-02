/*
 *  multiboot 2 boot loader header for xkern
 *  last edit aug_27_4:12_PM
 */

#include "multiboot2.h"
#include "multiboot.h"
#include "klog.h"

#define MAX_MMAP_ENTRIES 64
#define MAX_MODS         8

static struct boot_mem_map_entry g_mmap[MAX_MMAP_ENTRIES];
static struct multiboot_module   g_mods[MAX_MODS];
static char g_cmdline[256];
static char g_bootloader[64];

static u64 g_mb2_phys;

static int fb_masks_ok(u8 rp, u8 rs, u8 gp, u8 gs, u8 bp, u8 bs, u8 bpp)
{
    if (!rs || !gs || !bs)
        return 0;
    if (rs > 8 || gs > 8 || bs > 8)
        return 0;
    if ((u32)rp + rs > bpp || (u32)gp + gs > bpp || (u32)bp + bs > bpp)
        return 0;
    if (rp < gp + gs && gp < rp + rs)
        return 0;
    if (rp < bp + bs && bp < rp + rs)
        return 0;
    if (gp < bp + bs && bp < gp + gs)
        return 0;
    return 1;
}

u64 multiboot2_phys(void)
{
    return g_mb2_phys;
}

void multiboot2_parse(u64 mbi_phys, struct multiboot_info *out)
{
    u8 *p = (u8 *)(uintptr_t)mbi_phys;
    u32 total_size;
    struct mb2_tag *tag;

    g_mb2_phys = mbi_phys;

    for (u32 i = 0; i < MAX_MMAP_ENTRIES; i++)
        g_mmap[i].base = g_mmap[i].length = 0;

    out->flags = 0;
    out->mem_lower = 0;
    out->mem_upper = 0;
    out->cmdline = 0;
    out->boot_loader_name = 0;
    out->mods_count = 0;
    out->mods_addr = g_mods;
    out->mmap_count = 0;
    out->mmap_addr = g_mmap;
    out->framebuffer_addr = 0;
    out->framebuffer_pitch = 0;
    out->framebuffer_width = 0;
    out->framebuffer_height = 0;
    out->framebuffer_bpp = 0;
    out->framebuffer_type = MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT;

    if (!p)
        return;

    total_size = *(u32 *)p;

    for (u32 off = 8; off + sizeof(struct mb2_tag) <= total_size; ) {
        tag = (struct mb2_tag *)(p + off);
        if (tag->size < sizeof(struct mb2_tag))
            break;

        switch (tag->type) {
        case MB2_TAG_END:
            return;

        case MB2_TAG_CMDLINE: {
            struct mb2_tag_cmdline *t = (struct mb2_tag_cmdline *)tag;
            const char *s = t->string;
            u32 i = 0;

            for (; i < sizeof(g_cmdline) - 1 && s[i]; i++)
                g_cmdline[i] = s[i];
            g_cmdline[i] = '\0';
            out->cmdline = g_cmdline;
            out->flags |= MULTIBOOT_CMDLINE;
            break;
        }

        case MB2_TAG_BOOTLOADER: {
            struct mb2_tag_bootloader *t = (struct mb2_tag_bootloader *)tag;
            const char *s = t->string;
            u32 i = 0;

            for (; i < sizeof(g_bootloader) - 1 && s[i]; i++)
                g_bootloader[i] = s[i];
            g_bootloader[i] = '\0';
            out->boot_loader_name = g_bootloader;
            out->flags |= MULTIBOOT_BOOTLOADER_NAME;
            break;
        }

        case MB2_TAG_MODULE: {
            struct mb2_tag_module *t = (struct mb2_tag_module *)tag;
            const char *s = t->cmdline;
            u32 i = 0;

            if (out->mods_count >= MAX_MODS)
                break;
            g_mods[out->mods_count].mod_start = t->mod_start;
            g_mods[out->mods_count].mod_end = t->mod_end;
            for (; i < sizeof(g_mods[0].string) - 1 && s[i]; i++)
                g_mods[out->mods_count].string[i] = s[i];
            g_mods[out->mods_count].string[i] = '\0';
            out->mods_count++;
            out->flags |= MULTIBOOT_MODS_REQ;
            break;
        }

        case MB2_TAG_BASIC_MEMINFO: {
            struct mb2_tag_meminfo *t = (struct mb2_tag_meminfo *)tag;

            out->mem_lower = t->mem_lower;
            out->mem_upper = t->mem_upper;
            out->flags |= MULTIBOOT_MEM_INFO;
            break;
        }

        case MB2_TAG_MMAP: {
            struct mb2_tag_mmap *t = (struct mb2_tag_mmap *)tag;
            u8 *e = (u8 *)t->entries;
            u8 *end = (u8 *)tag + t->size;

            while (e + t->entry_size <= end &&
                   out->mmap_count < MAX_MMAP_ENTRIES) {
                struct mb2_mmap_entry *me = (struct mb2_mmap_entry *)e;
                struct boot_mem_map_entry *dst =
                    &g_mmap[out->mmap_count];

                dst->base = me->base;
                dst->length = me->length;
                dst->type = me->type;
                out->mmap_count++;
                e += t->entry_size;
            }
            out->flags |= MULTIBOOT_MMAP;
            break;
        }

        case MB2_TAG_FRAMEBUFFER: {
            struct mb2_tag_framebuffer *t = (struct mb2_tag_framebuffer *)tag;
            const u8 *ci = t->color_info;
            u8 b_pos, b_sz, g_pos, g_sz, r_pos, r_sz;

            out->framebuffer_addr = t->framebuffer_addr;
            out->framebuffer_pitch = t->framebuffer_pitch;
            out->framebuffer_width = t->framebuffer_width;
            out->framebuffer_height = t->framebuffer_height;
            out->framebuffer_bpp = t->framebuffer_bpp;
            out->framebuffer_type = t->framebuffer_type;

            /*
             * Two known layouts for the RGB colour info:
             *
             *  - Multiboot 2 spec (tag size 36): blue, green, red
             *    (position, size) pairs starting right after type.
             *
             *  - GRUB BIOS/VBE quirk (tag size 38): two zero bytes of
             *    padding after type, then red, green, blue pairs.
             *    Observed bytes: 00 00 r_pos r_sz g_pos g_sz b_pos b_sz.
             *
             * Try the spec layout first; if the channels don't form a
             * sane non-overlapping triple, use the GRUB layout.  fb.c
             * falls back to standard per-depth defaults if both fail.
             */
            b_pos = ci[0]; b_sz = ci[1];
            g_pos = ci[2]; g_sz = ci[3];
            r_pos = ci[4]; r_sz = ci[5];

            if (t->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB &&
                !fb_masks_ok(r_pos, r_sz, g_pos, g_sz, b_pos, b_sz,
                             t->framebuffer_bpp) &&
                t->size >= 38) {
                r_pos = ci[2]; r_sz = ci[3];
                g_pos = ci[4]; g_sz = ci[5];
                b_pos = ci[6]; b_sz = ci[7];
                klog("mb2", "fb tag: using GRUB BIOS colour layout");
            }

            out->framebuffer_blue_field_position  = b_pos;
            out->framebuffer_blue_mask_size       = b_sz;
            out->framebuffer_green_field_position = g_pos;
            out->framebuffer_green_mask_size      = g_sz;
            out->framebuffer_red_field_position   = r_pos;
            out->framebuffer_red_mask_size        = r_sz;
            out->flags |= MULTIBOOT_FRAMEBUFFER;
            break;
        }

        default:
            break;
        }

        off += (tag->size + 7u) & ~7u;      /* tags are 8-byte aligned */
    }
}
