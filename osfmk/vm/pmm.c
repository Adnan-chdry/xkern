#include "pmm.h"
#include "e820.h"
#include "klog.h"
#include "multiboot.h"

/*
 * Stack-based physical frame allocator.  Frames are tracked as full
 * 64-bit physical addresses; the identity map covers everything below
 * 4 GiB, and page-table pages above that are reached through a scratch
 * window by the paging layer.
 */
#define PMM_STACK_SIZE 65536
#define PMM_MAX_ADDR   0x0000C0000000ULL   /* collect frames below 3 GiB */
/* Never serve frames from the top 16 MiB of a range: firmware often
 * marks TSEG/SMRAM or stolen graphics memory as AVAILABLE up to the
 * very end of RAM.  Zeroing such a frame kills the machine silently
 * on the next SMI. */
#define PMM_TOP_GUARD  ((u64)16 << 20)

static u64 pmm_stack[PMM_STACK_SIZE];
static u32 pmm_stack_top;
static u64 pmm_total_memory;

extern char _kernel_start;
extern char _kernel_end;

void pmm_free(u64 addr)
{
    if (addr == 0)
        return;
    if (pmm_stack_top < PMM_STACK_SIZE)
        pmm_stack[pmm_stack_top++] = addr;
}

u64 pmm_alloc(void)
{
    if (pmm_stack_top == 0)
        return 0;
    return pmm_stack[--pmm_stack_top];
}

void pmm_reserve(u64 addr, u64 size)
{
    u64 start = addr & ~(PAGE_SIZE - 1);
    u64 end = addr + size;

    for (u64 a = start; a < end; a += PAGE_SIZE)
    {
        for (u32 i = 0; i < pmm_stack_top; i++)
        {
            if (pmm_stack[i] == a)
            {
                pmm_stack[i] = pmm_stack[--pmm_stack_top];
                break;
            }
        }
    }
}

static int pmm_is_kernel_region(u64 addr)
{
    u64 ks = (u64)&_kernel_start;
    u64 ke = (u64)&_kernel_end;
    return (addr >= ks && addr < ke);
}

void pmm_init(struct e820_entry *map, u32 count)
{
    pmm_stack_top = 0;
    pmm_total_memory = 0;

    for (u32 i = 0; i < count; i++)
    {
        uint64_t base = map[i].base;
        uint64_t length = map[i].length;
        uint64_t end;

        if (base >= PMM_MAX_ADDR)
            continue;
        if (base + length > PMM_MAX_ADDR)
            length = PMM_MAX_ADDR - base;

        end = base + length;

        if (map[i].type == MULTIBOOT_MEMORY_AVAILABLE)
        {
            u64 start = (base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            u64 stop = end & ~(PAGE_SIZE - 1);

            /* shrink the range away from its top edge */
            if (stop - start > PMM_TOP_GUARD * 2)
                stop -= PMM_TOP_GUARD;

            for (u64 addr = start; addr < stop; addr += PAGE_SIZE)
            {
                if (!pmm_is_kernel_region(addr))
                    pmm_free(addr);
            }

            pmm_total_memory += length;
        }
    }

    klog("pmm", "init done");
}

u64 pmm_get_total_memory(void)
{
    return pmm_total_memory;
}

u32 pmm_get_free_pages(void)
{
    return pmm_stack_top;
}
