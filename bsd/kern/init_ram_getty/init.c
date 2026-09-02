#include "init_ram_getty.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include "panic.h"
#include "pmm.h"
#include "paging.h"
#include "elf.h"
#include "klibc.h"

static u8 *g_initrd_addr;
static u32 g_initrd_size;
static int g_initrd_present;

static const char g_default_init[] =
    "clear\n"
    "echo XKERN builtin init (no initrd)\n"
    "version\n"
    "pid\n"
    "idle\n";

static void idle_main(void)
{
    klibc.printf("[idle] pid 0 idle loop\n");
    for (;;)
        asm volatile ("sti; hlt");
}

int initram_collect(struct multiboot_info *mbi, struct ramfs *fs)
{
    struct multiboot_module *mods;

    (void)fs;

    if (!mbi || !(mbi->flags & MULTIBOOT_MODS_REQ) || mbi->mods_count == 0)
        return -1;

    mods = mbi->mods_addr;
    g_initrd_addr = (u8 *)(uintptr_t)mods[0].mod_start;
    g_initrd_size = (u32)(mods[0].mod_end - mods[0].mod_start);
    g_initrd_present = 1;

    klog("initram", "initrd module %u bytes at 0x%llx",
         g_initrd_size, (unsigned long long)(uintptr_t)g_initrd_addr);
    return 0;
}

void initram_reserve(void)
{
    if (!g_initrd_present)
        return;

    paging_map_region((u64)(uintptr_t)g_initrd_addr,
                      (u64)(uintptr_t)g_initrd_addr,
                      g_initrd_size, PAGE_WRITE);
    pmm_reserve((u64)(uintptr_t)g_initrd_addr, g_initrd_size);
}

int initram_getty_init(struct ramfs *fs)
{
    struct task *idle;
    struct task *init_task;
    struct ramfs_file *init_file;
    void (*init_main)(void);
    void *init_arg = 0;    script_set_fs(fs);

    if (g_initrd_present && valid_cpio_magic((char *)g_initrd_addr)) {
        if (cpio_unpack(g_initrd_addr, g_initrd_size, fs) == 0) {
            ramfs_list(fs);
        } else {
            klog("initram", "initrd is not a valid cpio archive");
            panic("{init} use proper build method");
            return -1;
        }
    } else {
        klog("initram", "no initrd,panic()");
        panic("no_init()");
    }

    idle = task_create("idle", idle_main, 0);
    if (!idle) {
        klog("initram", "idle task create failed");
        panic("{init} task_create(idle) failed");
        return -1;
    }

    init_file = ramfs_lookup(fs, "/init");
    if (init_file && elf_valid(init_file->data, init_file->size)) {
        u32 entry;

        init_task = task_alloc("init", 0);
        if (!init_task) {
            klog("initram", "init task create failed");
            panic("{init} no_task()");
            return -1;
        }

        if (elf_load_pd(init_task->cr3, init_file->data, init_file->size,
                        &entry, 0, 0) != 0) {
            klog("initram", "failed to load /init ELF");
            init_task->state = TASK_ZOMBIE;
            panic("{init}elf_load_pd() failed");
            return -1;
        }
        {
            u32 argc, argv;

            argv = task_build_argv(init_task, "/init", &argc);
            task_set_entry(init_task, (void (*)(void))entry, argc, argv);
        }
        klog("initram", "/init is ELF, entry 0x%x", entry);
    } else {
        struct script_task_data *init_data = script_task_data_new();

        if (!init_data)
            return -1;
        klibc.strncpy(init_data->path, "/init", RAMFS_NAME_MAX - 1);
        init_data->path[RAMFS_NAME_MAX - 1] = '\0';
        init_main = script_task_main;
        init_arg = init_data;

        init_task = task_create("init", init_main, init_arg);
        if (!init_task) {
            klog("initram", "init task create failed");
            panic("{init} task_create(init) failed");
            return -1;
        }
    }

    klog("initram", "booting scheduler, init = pid 1");
    sched_boot(idle);

    for (;;)
        asm volatile ("sti; hlt");
}
