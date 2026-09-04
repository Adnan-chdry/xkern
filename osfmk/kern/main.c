/**
    OpenArc-1 (xkern-26.0.8)
    This version of xkern is based on Ark kernel written in 2024
    same author.just some new changes and modern design decisions.
                                                      ~ openArc-1
 **/

#include "uni.h" /*includes all the header files */
#include "klibc.h"
#include "game.h"
#include "pizza.h"
#include "doom.h"
#include "xrecovery.h"
#include "xdesktop.h"
#include "gpukit/lv_console.h"
#include "boot.h"
#include "xkos_gui/dbus/dbus.h"
#include "xkos_gui/de/xkos_de.h"
#include <hw-report.h>
#include "crypto/crypto_hash.h"
#include "crypto/crypto_cipher.h"

extern sh_main();
extern void smp_init(void);
extern demo(); //includes demo from libkern/libcpp/demo.c

static struct e820_entry g_e820_entries[E820_MAX_ENTRIES];
static struct ramfs g_initram_fs;
static struct multiboot_info saved_mbi;

extern void mem_e820_main(struct e820_entry *map, uint32_t count);
extern void multiboot2_parse(u64 mbi_phys, struct multiboot_info *out);

static uint32_t parse_mmap(struct multiboot_info *mbi)
{
    uint32_t count = 0;

    if (!(mbi->flags & MULTIBOOT_MMAP))
        return 0;

    for (u32 i = 0; i < mbi->mmap_count && count < E820_MAX_ENTRIES; i++) {
        const struct boot_mem_map_entry *entry = &mbi->mmap_addr[i];

        g_e820_entries[count].base = entry->base;
        g_e820_entries[count].length = entry->length;
        g_e820_entries[count].type = entry->type;
        g_e820_entries[count].attr = 0;
        count++;
    }

    return count;
}
void kernel_main(u64 mbi_phys)
{
    struct multiboot_info *info = &saved_mbi;
    uint32_t count;

    multiboot2_parse(mbi_phys, info);

    klibc.printf("%s\n", version);
    count = parse_mmap(info);
    initram_collect(info, &g_initram_fs);
    mem_e820_main(g_e820_entries, count);
    tsc_init();

    pmm_init(g_e820_entries, count);
    paging_init();
    dma_init();
    logger_init();
    dinit_init();
    gdt_details();

    //start of fb mbi
    klibc.printf("Multiboot2 info: 0x%lx\n",
           (unsigned long)mbi_phys);

    klibc.printf("Multiboot flags: 0x%08x\n",
           info->flags);

        if (info->flags & MULTIBOOT_FRAMEBUFFER) {
        klibc.printf("Framebuffer flag SET\n");
    } else {
        klibc.printf("Framebuffer flag NOT SET\n");
    }

    cpu_init();
    klibc.printf("::Target kernel_framebuff_init()\n");
    kernel_framebuff_init(&saved_mbi);
    restore_console0();
    acpi_init();
    initram_reserve();
    pic_init();
    idt_init();
    smp_init();
    syscall_init();
    pit_register_irq();
    //atkbd_register_irq();
    kernel_fpu_init(); //fpu.c
    klog("Kernel","proc_done");
    klog("crypto","cryptography proc has started");
    {
        u8 digest[SHA256_DIGEST_SIZE];
        u8 ct[64], pt[64];
        char hex[65];
        const u8 msg[] = "xkern crypto init";
        const u8 aes_key[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
                                16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
        const u8 aes_iv[16]  = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
        aes_ctx enc, dec;
        u32 ct_len, pt_len;

        klibc.printf("[crypto] starting self-test\n");

        sha256_hash(msg, sizeof(msg) - 1, digest);
        crypto_bytes_to_hex(digest, SHA256_DIGEST_SIZE, hex, sizeof(hex));
        klibc.printf("[crypto] SHA-256: %s\n", hex);

        aes_init(&enc, CIPHER_AES_256, MODE_CBC, aes_key, 32, aes_iv, 16);
        ct_len = aes_encrypt(&enc, msg, sizeof(msg) - 1, ct, sizeof(ct));
        klibc.printf("[crypto] AES encrypt done: %u bytes\n", ct_len);

        aes_init(&dec, CIPHER_AES_256, MODE_CBC, aes_key, 32, aes_iv, 16);
        pt_len = aes_decrypt(&dec, ct, ct_len, pt, sizeof(pt));
        klibc.printf("[crypto] AES decrypt done: %u bytes\n", pt_len);

        if (pt_len == sizeof(msg) - 1 && memcmp(pt, msg, pt_len) == 0)
            klibc.printf("[crypto] AES-256-CBC round-trip OK\n");
        else
            klibc.printf("[crypto] AES-256-CBC round-trip FAILED\n");
    }


    __init();
    klog("kernel","kernel_main() done");

}

void __init(){
    __sub_boot_domain();
    //demo();
    pit_init(1000);
    atkbd_init();
    true_smbios_init();
    pci_scan();
    usb_setup(); //requires pci_scan();
    pci_list();

    /* scan hardware and populate /dev nodes */
    hw_report_scan();

    //  re-enable it when not testing in a real machine
   // atmouse_init();
  //  atmouse_register_irq();



    //developer service registry: start everything registered so far
    io_service_init();
    //network stack included
    //io_service_register(&ionet_service);
    io_service_start_all();

    //xk install demo
    klog("kernel.RootKit.GPUkit.xrecovery","xrecovery_run() on work");
    klog("kernel.RootKit.GPUkit.xrecovery","xrecovery_run(fail) on intention 126-127{main.c}");

       /* switch from console0 (plymouth splash) to console1 (kernel text) */
    switch_console();
  __klog("kernel","all proc done\n");
  if (lv_console_active()) //for the screen push
      lv_console_settle();
   // initram_getty_init(&g_initram_fs);
    panic("unexepected reason");
}

void true_smbios_init(){
    struct smbios_info smbios;
    if(smbios_init(&smbios)){
        klog("kernel.smBIOS","model found at %u.%u\n",smbios.major,smbios.minor);
        smbios_print(&smbios);
    }
    else {
        klog("kernel.smBIOS","appopriate address wasnt found");
    }
}



/*
  framebuffer design
*/
void kernel_framebuff_init(struct multiboot_info *mbi)
{
    klibc.printf("Framebuffer flag: %s\n",
           (mbi->flags & MULTIBOOT_FRAMEBUFFER)
               ? "SET"
               : "NOT SET");

    klibc.printf("Framebuffer address: 0x%llx\n",
           (unsigned long long)mbi->framebuffer_addr);

    klibc.printf("Framebuffer width:   %u\n",
           mbi->framebuffer_width);

    klibc.printf("Framebuffer height:  %u\n",
           mbi->framebuffer_height);

    klibc.printf("Framebuffer pitch:   %u\n",
           mbi->framebuffer_pitch);

    klibc.printf("Framebuffer bpp:     %u\n",
           mbi->framebuffer_bpp);

    klibc.printf("Framebuffer type:    %u\n",
           mbi->framebuffer_type);

    klibc.printf("Red position:        %u\n",
           mbi->framebuffer_red_field_position);

    klibc.printf("Red mask:            %u\n",
           mbi->framebuffer_red_mask_size);

    klibc.printf("Green position:      %u\n",
           mbi->framebuffer_green_field_position);

    klibc.printf("Green mask:          %u\n",
           mbi->framebuffer_green_mask_size);

    klibc.printf("Blue position:       %u\n",
           mbi->framebuffer_blue_field_position);

    klibc.printf("Blue mask:           %u\n",
           mbi->framebuffer_blue_mask_size);

    if (!(mbi->flags & MULTIBOOT_FRAMEBUFFER) ||
        mbi->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT ||
        mbi->framebuffer_pitch == 0) {
        klog("kernel.HWframe", "no graphics framebuffer, using VGA text mode");
        return;
    }

    framebuffer_init(
        mbi->framebuffer_addr,
        mbi->framebuffer_width,
        mbi->framebuffer_height,
        mbi->framebuffer_pitch,
        mbi->framebuffer_bpp,

        mbi->framebuffer_red_mask_size,
        mbi->framebuffer_red_field_position,

        mbi->framebuffer_green_mask_size,
        mbi->framebuffer_green_field_position,

        mbi->framebuffer_blue_mask_size,
        mbi->framebuffer_blue_field_position
    );

    /* font set selection: boot-arg "console=font6x12" (default: 9x8) */
    if (mbi->flags & MULTIBOOT_CMDLINE) {
        const char *cmdline = mbi->cmdline;
        if (cmdline && strstr(cmdline, "console=font6x12")) {
            font_select(FONT_6X12);
            klog("kernel.font", "console font set: spleen 6x12");
        }
    }

    font_init(mbi);

    klog("kernel.HWframe", "framebuffer initialized: %ux%u %u bpp @ 0x%llx\n",
         mbi->framebuffer_width,
         mbi->framebuffer_height,
         mbi->framebuffer_bpp,
         (unsigned long long)mbi->framebuffer_addr);
    framebuffer_clear(0x000000);

    /* LVGL console: owns all printf/klog output from here on.
     * Falls back to the bitmap-font console if LVGL cannot start. */
    if (lv_console_start(mbi->framebuffer_width, mbi->framebuffer_height)) {
        klog("kernel.LVGL", "LVGL console active %ux%u",
             mbi->framebuffer_width, mbi->framebuffer_height);
        klibc.printf("%s\n", version);  /* re-print banner into the GUI console */
    } else {
        klog("kernel.LVGL", "LVGL console unavailable, font console in use");
    }
}



void __sub_boot_domain(){
    //kernel_start_dhcp(host_model,dmc); //calls host model and initialized on dmc(dynamic manim conv) and setups on automatically
    //kernel_dhcp_poll("0:0000","port:1");
    //kenrel_open(type_console,"/net/console0",kernel_port(dmc));

    if (0){ //replace 0 with console in future provided by kernel_open
    //kernel_shift_mode("console0");
        return;
    } else {
        klog("kernel.domain.boot","connection can't be made");
    }
}
/*
    gdt maps
*/
struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static inline void read_gdtr(struct gdtr *g)
{
    __asm__ volatile ("sgdt %0" : "=m"(*g));
}
void gdt_details(void)
{
    struct gdtr gdtr;

    read_gdtr(&gdtr);

    klog("kernel.Gdt", "GDTR:");
    klog("kernel.Gdt",
         "\tbase  = 0x%llx"
         "\n\tlimit = 0x%04x",
         (unsigned long long)gdtr.base,
         gdtr.limit
    );
}
