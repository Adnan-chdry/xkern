#ifndef LOADER_CONFIG_H
#define LOADER_CONFIG_H

#include "multiboot.h"

/*
 * macOS-style kernel level configuration ("boot-args").
 *
 * GRUB enters loader_main() instead of kernel_main().  The loader is a
 * temporary pre-kernel environment: it brings up just enough hardware
 * (IDT, PIC, PIT, keyboard), paints a graphical splash on the multiboot
 * framebuffer and drops into a small shell where boot-args can be edited
 * before the `boot` command hands control to kernel_main().
 */

#define BOOT_ARGS_MAX        256
#define LOADER_BOOT_DELAY_S  5

struct boot_config {
    char args[BOOT_ARGS_MAX];     /* canonical boot-args string            */
    char cmdline[BOOT_ARGS_MAX];  /* final cmdline handed to the kernel    */
    int  verbose;                 /* -v  verbose logging                   */
    int  single_user;             /* -s  single user mode                  */
    int  safe_mode;               /* -x  safe mode                         */
    int  ignore_caches;           /* -f  ignore caches / re-read everything */
};

/* entry point (called from arch/boot.asm) */
void loader_main(struct multiboot_info *mbi);

/* boot-args management */
void loader_set_args(const char *args);
const char *loader_get_args(void);
struct boot_config *boot_config_get(void);

/* kernel side queries: boot_arg_flag("v"), boot_arg_value("debug") */
int boot_arg_flag(const char *name);
const char *boot_arg_value(const char *key);
int boot_verbose(void);

#endif
