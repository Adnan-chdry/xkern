/*
 * loader_config.c - xkern pre-boot loader environment.
 *
 * Temporary environment that runs before the kernel proper.  It initialises
 * only what it needs (VGA/framebuffer console, IDT, PIC, PIT, keyboard),
 * shows a graphical splash and offers a shell to edit macOS-style boot-args
 * before `boot` hands the saved multiboot info to kernel_main().
 */

#include "loader_config.h"
#include "kernel.h"
#include "multiboot2.h"
#include "vga.h"
#include "idt.h"
#include "pic.h"
#include "io.h"
#include "stdio.h"
#include "string.h"
#include "stdarg.h"
#include "atkbd.h"
#include "pit.h"
#include "tsc.h"
#include "klibc.h"
#include "IOGraphicsFamily/fb.h"
#include "IOGraphicsFamily/font.h"

#define FONT_W        9
#define FONT_H        8

#define LCLR_BG       0x00101218    /* splash background            */
#define LCLR_FG       0x00E8E8ED    /* normal text                  */
#define LCLR_DIM      0x008E8E93    /* secondary text               */
#define LCLR_ACCENT   0x000A84FF    /* macOS system blue            */

static struct multiboot_info *g_mbi;
static struct boot_config g_cfg;

static int fb_console;                /* graphical console available  */
static u32 lrow, lcol;                /* loader console cursor        */
static u32 lcols, lrows;              /* console size in glyphs       */
static u32 lfg = LCLR_FG;
static u32 lbg = LCLR_BG;

/* ===================================================================== */
/*  temporary console layer (framebuffer when present, VGA text else)     */
/* ===================================================================== */

static void lputc(char c)
{
    if (!fb_console) {
        if (c == '\b') { vga_backspace(); return; }
        vga_putchar(c);
        return;
    }

    switch (c) {
    case '\n':
        lcol = 0;
        lrow++;
        break;
    case '\r':
        lcol = 0;
        break;
    case '\b':
        if (lcol > 0) {
            lcol--;
            font_draw_glyph(lcol * FONT_W, lrow * FONT_H, ' ', lfg, lbg);
        }
        return;
    default:
        if (c >= 0x20 && c < 0x7F) {
            font_draw_glyph(lcol * FONT_W, lrow * FONT_H, c, lfg, lbg);
            lcol++;
        }
        break;
    }

    if (lcol >= lcols) { lcol = 0; lrow++; }
    if (lrow >= lrows) {
        framebuffer_scroll_up(FONT_H);
        lrow = lrows - 1;
    }
}

static void lputs(const char *s)
{
    while (*s)
        lputc(*s++);
}

static void lprintf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    klibc.vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lputs(buf);
}

static void lcenter(const char *s)
{
    unsigned int len = strlen(s);
    lcol = (lcols > len) ? (lcols - len) / 2 : 0;
    lputs(s);
    lputc('\n');
}

static void read_line(char *buf, int max)
{
    int i = 0;

    for (;;) {
        int c = getchar();
        if (c < 0)
            continue;
        if (c == '\n' || c == '\r') {
            lputc('\n');
            break;
        }
        if (c == '\b' || c == 0x7F) {
            if (i > 0) {
                i--;
                lputc('\b');
            }
            continue;
        }
        if (c >= 0x20 && c < 0x7F && i < max - 1) {
            buf[i++] = (char)c;
            lputc((char)c);
        }
    }
    buf[i] = '\0';
}

/* ===================================================================== */
/*  boot-args parsing (macOS style: -v -s -x -f key=value)                */
/* ===================================================================== */

static void parse_args(char *s)
{
    char normalized[BOOT_ARGS_MAX];
    unsigned int n = 0;

    g_cfg.verbose = 0;
    g_cfg.single_user = 0;
    g_cfg.safe_mode = 0;
    g_cfg.ignore_caches = 0;

    while (*s) {
        char *start;
        int len;

        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s)
            break;
        start = s;
        while (*s && *s != ' ' && *s != '\t')
            s++;
        len = (int)(s - start);

        /* known single letter flags */
        if (len == 2 && start[0] == '-') {
            switch (start[1]) {
            case 'v': g_cfg.verbose = 1; break;
            case 's': g_cfg.single_user = 1; break;
            case 'x': g_cfg.safe_mode = 1; break;
            case 'f': g_cfg.ignore_caches = 1; break;
            }
        }

        /* keep every token so unknown args survive untouched */
        if (n && n < sizeof(normalized) - 1)
            normalized[n++] = ' ';
        if ((unsigned int)len > sizeof(normalized) - 1 - n)
            len = (int)(sizeof(normalized) - 1 - n);
        memcpy(normalized + n, start, len);
        n += len;
    }
    normalized[n] = '\0';
    strcpy(g_cfg.args, normalized);
}

void loader_set_args(const char *args)
{
    char tmp[BOOT_ARGS_MAX];

    strncpy(tmp, args ? args : "", sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    parse_args(tmp);
}

const char *loader_get_args(void)
{
    return g_cfg.args;
}

struct boot_config *boot_config_get(void)
{
    return &g_cfg;
}

int boot_arg_flag(const char *name)
{
    const char *p = g_cfg.args;
    unsigned int len = strlen(name);

    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        if (!strncmp(p, name, len) &&
            (p[len] == '\0' || p[len] == ' '))
            return 1;
        while (*p && *p != ' ')
            p++;
    }
    return 0;
}

const char *boot_arg_value(const char *key)
{
    static char empty[2] = "";
    const char *p = g_cfg.args;
    unsigned int len = strlen(key);

    while (*p) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        if (!strncmp(p, key, len) && p[len] == '=')
            return p + len + 1;
        while (*p && *p != ' ')
            p++;
    }
    return empty;
}

int boot_verbose(void)
{
    return g_cfg.verbose;
}

/* ===================================================================== */
/*  splash / banner                                                       */
/* ===================================================================== */

static void draw_splash(void)
{
    const char *args = g_cfg.args;

    if (!fb_console) {
        lputs("\n");
        lputs("==============================================================\n");
        lputs("   XKERN LOADER :: pre-boot configuration environment\n");
        lputs("==============================================================\n");
        lprintf("   boot-args: %s\n", args[0] ? args : "(none)");
        lputs("\n");
        return;
    }

    framebuffer_clear(LCLR_BG);

    /* accent bar above the title */
    framebuffer_fill_rect(0, lrows * FONT_H / 3 - 8, lcols * FONT_W, 2, LCLR_ACCENT);

    lrow = lrows / 3;
    lfg = LCLR_ACCENT;
    lcenter("XKERN LOADER");
    lfg = LCLR_DIM;
    lcenter("pre-boot configuration environment");
    lfg = LCLR_FG;
    lputc('\n');
    lprintf("  boot-args: %s\n", args[0] ? args : "(none)");
    framebuffer_flush();
}

/* ===================================================================== */
/*  minimal hardware init for the temporary environment                   */
/* ===================================================================== */

static void loader_hw_init(void)
{
    vga_init();
    tsc_init();

    idt_init();
    pic_init();
    pit_register_irq();
    atkbd_register_irq();
    pit_init(1000);
    atkbd_init();

    __asm__ volatile ("sti");
}

/* ===================================================================== */
/*  actions                                                               */
/* ===================================================================== */

static void machine_reboot(void)
{
    u8 status;

    lputs("rebooting...\n");
    do {
        status = inb(0x64);
    } while (status & 0x02);
    outb(0x64, 0xFE);           /* pulse 8042 reset line */
    for (;;)
        __asm__ volatile ("hlt");
}

static void machine_halt(void)
{
    lputs("halted.\n");
    __asm__ volatile ("cli");
    for (;;)
        __asm__ volatile ("hlt");
}

static void boot_kernel(void)
{
    strcpy(g_cfg.cmdline, g_cfg.args);

    lputs("\n");
    lprintf("kernel    : /boot/xkern\n");
    lprintf("boot-args : %s\n", g_cfg.cmdline[0] ? g_cfg.cmdline : "(none)");
    if (g_cfg.verbose)      lputs("mode      : verbose\n");
    if (g_cfg.single_user)  lputs("mode      : single user\n");
    if (g_cfg.safe_mode)    lputs("mode      : safe mode\n");
    if (g_cfg.ignore_caches) lputs("mode      : ignore caches\n");
    lputs("handing off to kernel_main()...\n");

    if (fb_console)
        framebuffer_flush();
    pit_sleep(300);

    /* re-enter the kernel with the saved Multiboot 2 information */
    kernel_main(multiboot2_phys());

    /* kernel_main() is not expected to return */
    lputs("kernel_main() returned; halting.\n");
    machine_halt();
}

/* ===================================================================== */
/*  loader shell                                                          */
/* ===================================================================== */

static void cmd_help(void)
{
    lputs("commands:\n");
    lputs("  boot             boot the kernel with the current boot-args\n");
    lputs("  args             show current boot-args\n");
    lputs("  set <args...>    replace boot-args (e.g. set -v debug=0x14e)\n");
    lputs("  add <arg>        append one arg to boot-args\n");
    lputs("  clear            remove all boot-args\n");
    lputs("  info             show bootloader provided information\n");
    lputs("  reboot           reset the machine\n");
    lputs("  halt             halt the CPU\n");
    lputs("  help             this list\n");
    lputs("\nflags: -v verbose  -s single user  -x safe mode  -f ignore caches\n");
}

static void cmd_args(void)
{
    lprintf("boot-args: %s\n", g_cfg.args[0] ? g_cfg.args : "(none)");
}

static void cmd_set(char *rest)
{
    loader_set_args(rest);
    cmd_args();
}

static void cmd_add(char *rest)
{
    if (!rest || !rest[0]) {
        lputs("usage: add <arg>\n");
        return;
    }
    if (strlen(g_cfg.args) + 1 + strlen(rest) >= BOOT_ARGS_MAX) {
        lputs("error: boot-args full\n");
        return;
    }
    if (g_cfg.args[0])
        strcat(g_cfg.args, " ");
    strcat(g_cfg.args, rest);
    parse_args(g_cfg.args);
    cmd_args();
}

static void cmd_info(void)
{
    if (g_mbi->flags & MULTIBOOT_BOOTLOADER_NAME)
        lprintf("bootloader : %s\n", (const char *)g_mbi->boot_loader_name);
    if (g_mbi->flags & MULTIBOOT_MEM_INFO) {
        lprintf("memory     : lower %u KiB, upper %u KiB\n",
                g_mbi->mem_lower, g_mbi->mem_upper);
    }
    if (g_mbi->flags & MULTIBOOT_CMDLINE)
        lprintf("cmdline    : %s\n", (const char *)g_mbi->cmdline);
    if (g_mbi->flags & MULTIBOOT_FRAMEBUFFER)
        lprintf("framebuffer: %ux%u %u bpp (type %u)\n",
                g_mbi->framebuffer_width, g_mbi->framebuffer_height,
                g_mbi->framebuffer_bpp, g_mbi->framebuffer_type);
    lprintf("console    : %s\n", fb_console ? "graphical (multiboot fb)" : "vga text");
}

static void exec_cmd(char *line)
{
    char *cmd = line;
    char *rest;

    while (*cmd == ' ')
        cmd++;
    if (!*cmd)
        return;
    rest = cmd;
    while (*rest && *rest != ' ')
        rest++;
    if (*rest) {
        *rest++ = '\0';
        while (*rest == ' ')
            rest++;
    }

    if (!strcmp(cmd, "help"))        cmd_help();
    else if (!strcmp(cmd, "?"))      cmd_help();
    else if (!strcmp(cmd, "boot"))   boot_kernel();
    else if (!strcmp(cmd, "args"))   cmd_args();
    else if (!strcmp(cmd, "show"))   cmd_args();
    else if (!strcmp(cmd, "set"))    cmd_set(rest);
    else if (!strcmp(cmd, "add"))    cmd_add(rest);
    else if (!strcmp(cmd, "clear")) { loader_set_args(""); cmd_args(); }
    else if (!strcmp(cmd, "info"))   cmd_info();
    else if (!strcmp(cmd, "reboot")) machine_reboot();
    else if (!strcmp(cmd, "halt"))   machine_halt();
    else lprintf("unknown command '%s' (try 'help')\n", cmd);
}

static void loader_shell(void)
{
    char line[BOOT_ARGS_MAX];

    lfg = LCLR_DIM;
    lputs("\n-- temporary loader shell -- type 'help' for commands --\n");
    lfg = LCLR_FG;

    for (;;) {
        lputs("loader> ");
        read_line(line, sizeof(line));
        exec_cmd(line);
    }
}

/* ===================================================================== */
/*  auto-boot countdown                                                   */
/* ===================================================================== */

/* returns 1 when the timeout expired (auto boot), 0 on keypress */
static int countdown(int seconds)
{
    u32 deadline = pit_get_ticks() + (u32)seconds * 1000u;
    int last = -1;

    for (;;) {
        int remaining;
        int c = atkbd_getchar();

        if (c >= 0)
            return 0;

        remaining = (int)((deadline - pit_get_ticks()) / 1000u) + 1;
        if (remaining != last) {
            last = remaining;
            lprintf("\rbooting in %d s - press any key for boot options ",
                    remaining > 0 ? remaining : 0);
            if (fb_console)
                framebuffer_flush();
        }
        if ((int)(deadline - pit_get_ticks()) <= 0)
            return 1;
    }
}

/* ===================================================================== */
/*  entry point                                                           */
/* ===================================================================== */

void loader_main(struct multiboot_info *mbi)
{
    g_mbi = mbi;

    /* console first: framebuffer when GRUB gave us one, VGA text else */
    if ((mbi->flags & MULTIBOOT_FRAMEBUFFER) &&
        mbi->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT &&
        mbi->framebuffer_pitch != 0) {
        framebuffer_init(mbi->framebuffer_addr,
                         mbi->framebuffer_width,
                         mbi->framebuffer_height,
                         mbi->framebuffer_pitch,
                         mbi->framebuffer_bpp,
                         mbi->framebuffer_red_mask_size,
                         mbi->framebuffer_red_field_position,
                         mbi->framebuffer_green_mask_size,
                         mbi->framebuffer_green_field_position,
                         mbi->framebuffer_blue_mask_size,
                         mbi->framebuffer_blue_field_position);
        font_init(mbi);
        lcols = font_columns();
        lrows = font_rows();
        fb_console = (lcols > 0 && lrows > 0);
    }
    if (!fb_console) {
        lcols = 80;
        lrows = 25;
    }
    framebuffer_clear(0x000000);
    loader_hw_init();
    draw_splash();

    if (countdown(LOADER_BOOT_DELAY_S)) {
        lputc('\n');
        boot_kernel();
    }
    loader_shell();
}
