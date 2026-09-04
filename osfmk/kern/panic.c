/*
    xkern kernel panic system with full back trace system
    in future the log will be exported to the blk device
    !!kernel family
*/
#include <stdint.h>
#include <klog.h>
#include "../../pexpert/x86_64/cpu.h"
#include <kernel.h>
#include "init_ram_getty.h"
#include <pic.h>
#include <vga.h>
#include "io.h"
#include "paging.h"
#include "tsc.h"
#include "gpukit/lv_console.h"
#include "string.h"
#include "klibc.h"
#include "version.h"

#define PANIC_MAX_BACKTRACE 16


struct panic_gdtr {
    u16 limit;
    u64 base;
} __attribute__((packed));

/* canonical kernel/user pointer sanity: low identity-mapped RAM plus the
 * higher-half kernel window */
#define VALID_PTR(p) ((unsigned long)(p) >= 0x00100000UL && \
                      (unsigned long)(p) <= 0x00007FFFFFFFFFFFUL && \
                      ((unsigned long)(p) & 7) == 0)

struct x86_64_frame {
    struct x86_64_frame *rbp;
    u64 rip;
};


static u64 read_cr2(void)
{
    u64 v;
    asm volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

static u64 read_cr3(void)
{
    u64 v;
    asm volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

static void read_idtr(struct panic_gdtr *g)
{
    asm volatile ("sidt %0" : "=m"(*g));
}

static void pic_in_service(u8 *master, u8 *slave)
{
    outb(0x20, 0x0B);           /* OCW3: read ISR on read of PIC1_CMD */
    *master = inb(0x20);
    outb(0xA0, 0x0B);
    *slave = inb(0xA0);
}

static void panic_backtrace(void)
{
    struct x86_64_frame *f = (struct x86_64_frame *)__builtin_frame_address(0);

    klibc.printf("Backtrace (CPU 0), Frame : Return Address\n");

    for (int i = 0; i < PANIC_MAX_BACKTRACE && f; i++) {
        if (!VALID_PTR(f))
            break;

        klibc.printf("<0x%016lx> : <0x%016lx>\n",
                     (unsigned long)f, (unsigned long)f->rip);

        struct x86_64_frame *next = f->rbp;
        if (next == f || !VALID_PTR(next))
            break;
        f = next;
    }
}


static void panic_page_tables(u64 fault_addr)
{
    u64 cr3 = read_cr3();
    u64 a = PML4_INDEX(fault_addr), b = PDPT_INDEX(fault_addr);
    u64 c = PD_INDEX(fault_addr), d = PT_INDEX(fault_addr);
    u64 *pml4 = (u64 *)(cr3 & ~0xFFFULL);

    klibc.printf("CR2 (fault address): <0x%016lx>\n", (unsigned long)fault_addr);
    klibc.printf("CR3 (PML4 phys):     <0x%016lx>\n", (unsigned long)cr3);

    klibc.printf("PML4E[0..7]:\n");
    for (u64 i = 0; i < 8; i++)
        klibc.printf("  PML4E[%lu] = <0x%016lx>\n", i, pml4[i]);

    if (!(pml4[a] & 1)) {
        klibc.printf("  PML4[%lu] not present - CR2 region unmapped\n", a);
        return;
    }

    u64 *pdpt = (u64 *)(pml4[a] & ~0xFFFULL);
    if (!(pdpt[b] & 1)) {
        klibc.printf("  PDPT[%lu] not present\n", b);
        return;
    }
    klibc.printf("  PDPTE[%lu] = <0x%016lx>\n", b, pdpt[b]);

    u64 *pd = (u64 *)(pdpt[b] & ~0xFFFULL);
    if (!(pd[c] & 1)) {
        klibc.printf("  PDE  [%lu] not present\n", c);
        return;
    }
    klibc.printf("  PDE  [%lu] = <0x%016lx%s>\n",
                 c, pd[c], (pd[c] & 0x80) ? " (2MiB)" : "");

    if (!(pd[c] & 0x80)) {
        u64 *pt = (u64 *)(pd[c] & ~0xFFFULL);
        klibc.printf("  PTE  [%lu] = <0x%016lx>\n", d, pt[d]);
    }
}

/* ------------------------------------------------------------------ */
/*  panic                                                             */
/* ------------------------------------------------------------------ */

void panic(const char *msg)
{
    /*cpu related routines*/
    cpu_info_t cpu;
    cpu_get_info(&cpu);

    /*other*/
    u64 caller = (u64)__builtin_return_address(0);
    u8 irq_m = 0, irq_s = 0;

    pic_in_service(&irq_m, &irq_s);

    pic_disable_irq(0);                     /* stop the timer tick */
    asm volatile ("cli");

    struct task *t = g_cur_task;

    klibc.printf("panic(cpu 0 caller <0x%016lx>): %s\n",
                 (unsigned long)caller, msg ? msg : "?");

    panic_backtrace();
    e820_print();

    {
        struct panic_gdtr gdtr = {0, 0}, idtr_l = {0, 0};
        u64 cr0, rflags;
        asm volatile ("mov %%cr0, %0" : "=r"(cr0));
        asm volatile ("pushfq; popq %0" : "=r"(rflags));
        asm volatile ("sgdt %0" : "=m"(gdtr));
        read_idtr(&idtr_l);

        klibc.printf("CPU state:\n");
        klibc.printf("  CR0 = <0x%016lx>  RFLAGS = <0x%016lx>\n",
                     (unsigned long)cr0, (unsigned long)rflags);
        klibc.printf("  GDT base = <0x%016llx> limit = <0x%04x>\n",
                     (unsigned long long)gdtr.base, gdtr.limit);
        klibc.printf("  IDT base = <0x%016llx> limit = <0x%04x>\n",
                     (unsigned long long)idtr_l.base, idtr_l.limit);
    }

    klibc.printf("IRQs in service: master=<0x%02x> slave=<0x%02x>\n", irq_m, irq_s);

    panic_page_tables(read_cr2());

    klibc.printf("BSD process name corresponding to current thread: %s\n",
                 (t && t->name[0]) ? t->name : "Unknown");

    klibc.printf("OS version: %s\n", osrelease);
    klibc.printf("Kernel version: %s\n", version);
    klibc.printf("proccesor <%s::%s>\n",cpu.vendor,cpu.name);
    klibc.printf("kArch: %s\n",arch);
    klog_lvl(KLOG_EMERG, "kernel", "panic: %s - system halted", msg ? msg : "?");

    /* push the console fully to the bottom before execution stops */
    if (lv_console_active())
        lv_console_settle();
    tsc_disable();                          /* no timestamps past this point */

    for (;;)
        asm volatile ("cli");
        asm volatile ("hlt");
}

void exc_page_fault(u64 eip, u64 cr2)
{
    struct task *t = g_cur_task;
    u64 esp = 0, cr3 = 0, stk = 0;

    if (t) {
        esp = t->sp;
        cr3 = t->cr3;
        stk = t->stack;
    }
    klog("exc", "page fault rip=%llx cr2=%llx pid=%x sp=%llx cr3=%llx stack=%llx",
         eip, cr2, t ? t->pid : 0xffffffffu, esp, cr3, stk);
}
