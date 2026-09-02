#include <cstdint>
#include <cstddef>

extern "C" {
#include "pmm.h"
#include "klog.h"
}

/* ------------------------------------------------------------------ */
/*  Minimal heap allocator for operator new / operator delete          */
/*                                                                    */
/*  Grows by 4 KiB pages from the physical-memory manager.  Allocated */
/*  blocks are never returned until heap_shutdown() (or reboot).       */
/*  Allocations are rounded up to 16-byte alignment.                  */
/* ------------------------------------------------------------------ */

static constexpr u32 HEAP_ALIGN   = 16;
static constexpr u32 PAGE_SZ      = 4096;

static u8  *heap_ptr  = nullptr;
static u8  *heap_end  = nullptr;
static u64  heap_phys = 0;       /* physical page backing current span */
static u64  heap_pages = 0;      /* total pages allocated to heap      */

static u8 *heap_grow(u32 need)
{
    u32 pages = (need + PAGE_SZ - 1) / PAGE_SZ;
    if (pages < 1) pages = 1;

    for (u32 i = 0; i < pages; i++) {
        u64 page = pmm_alloc();
        if (!page)
            return nullptr;

        u64 virt = page;   /* identity-mapped */
        u8 *base = reinterpret_cast<u8 *>(virt);

        if (!heap_ptr) {
            heap_ptr = base;
            heap_end = base + PAGE_SZ;
            heap_phys = page;
        } else {
            heap_end = base + PAGE_SZ;
        }
        heap_pages++;
    }

    return heap_ptr;
}

static void *heap_alloc(u32 size)
{
    u32 aligned = (size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);

    if (!heap_ptr || (u32)(heap_end - heap_ptr) < aligned) {
        if (!heap_grow(aligned))
            return nullptr;
    }

    void *p = heap_ptr;
    heap_ptr += aligned;
    return p;
}

extern "C" {

/* ------------------------------------------------------------------ */
/*  operator new / operator delete                                     */
/* ------------------------------------------------------------------ */

void *operator new(size_t size)
{
    if (size == 0) size = 1;
    void *p = heap_alloc(static_cast<u32>(size));
    if (!p) {
        klog_lvl(KLOG_ERR, "libcpp", "operator new: out of memory (%u bytes)", (u32)size);
        for (;;) asm volatile ("hlt");
    }
    return p;
}

void *operator new[](size_t size)
{
    return operator new(size);
}

void *operator new(size_t size, void *ptr) noexcept
{
    (void)size;
    return ptr;
}

void *operator new[](size_t size, void *ptr) noexcept
{
    return operator new(size, ptr);
}

void operator delete(void *ptr) noexcept
{
    (void)ptr;
    /* No-op: heap is bump-allocated, individual frees are ignored.
     * The heap is only reclaimed on reboot. */
}

void operator delete[](void *ptr) noexcept
{
    operator delete(ptr);
}

void operator delete(void *ptr, size_t) noexcept
{
    operator delete(ptr);
}

void operator delete[](void *ptr, size_t) noexcept
{
    operator delete(ptr);
}

/* ------------------------------------------------------------------ */
/*  C++ ABI runtime stubs                                              */
/* ------------------------------------------------------------------ */

/* atexit / static destructor support */
struct atexit_entry {
    void (*dtor)(void *);
    void *obj;
    void *dso;
};

static constexpr u32 ATEXIT_MAX = 128;
static atexit_entry g_atexit_table[ATEXIT_MAX];
static u32 g_atexit_count = 0;

int __cxa_atexit(void (*dtor)(void *), void *obj, void *dso)
{
    if (g_atexit_count >= ATEXIT_MAX) {
        klog_lvl(KLOG_ERR, "libcpp", "__cxa_atexit: table full");
        return -1;
    }
    g_atexit_table[g_atexit_count].dtor = dtor;
    g_atexit_table[g_atexit_count].obj  = obj;
    g_atexit_table[g_atexit_count].dso  = dso;
    g_atexit_count++;
    return 0;
}

void __cxa_finalize(void *dso)
{
    if (!dso) {
        /* Shutdown all: call dtors in reverse order */
        for (u32 i = g_atexit_count; i > 0; i--) {
            g_atexit_table[i - 1].dtor(g_atexit_table[i - 1].obj);
        }
        g_atexit_count = 0;
    } else {
        /* Shutdown only entries matching this DSO */
        for (u32 i = g_atexit_count; i > 0; i--) {
            if (g_atexit_table[i - 1].dso == dso) {
                g_atexit_table[i - 1].dtor(g_atexit_table[i - 1].obj);
                /* compact */
                for (u32 j = i - 1; j < g_atexit_count - 1; j++)
                    g_atexit_table[j] = g_atexit_table[j + 1];
                g_atexit_count--;
            }
        }
    }
}

void __cxa_pure_virtual(void)
{
    klog_lvl(KLOG_ERR, "libcpp", "__cxa_pure_virtual: called!");
    for (;;) asm volatile ("hlt");
}

void __cxa_deleted_virtual(void)
{
    klog_lvl(KLOG_ERR, "libcpp", "__cxa_deleted_virtual: called!");
    for (;;) asm volatile ("hlt");
}

/* Guard variables for thread-safe static local initialization (Itanium ABI).
 * Single-threaded kernel: just use a byte flag per guard. */
static constexpr u32 GUARD_MAX = 256;
static u8 g_guard_buf[GUARD_MAX];

int __cxa_guard_acquire(long *guard)
{
    long idx = *guard;
    if (idx == 0) {
        /* First time: allocate a guard slot */
        static long next_guard = 1;
        if (next_guard < GUARD_MAX) {
            idx = next_guard++;
            *guard = idx;
        } else {
            klog_lvl(KLOG_ERR, "libcpp", "__cxa_guard_acquire: table full");
            return 0;
        }
    }
    return (g_guard_buf[idx] == 0) ? 1 : 0;
}

void __cxa_guard_release(long *guard)
{
    long idx = *guard;
    if (idx > 0 && idx < GUARD_MAX)
        g_guard_buf[idx] = 1;
}

void __cxa_guard_abort(long *guard)
{
    long idx = *guard;
    if (idx > 0 && idx < GUARD_MAX)
        g_guard_buf[idx] = 0;
}

/* Dummy __cxa_allocate_exception / __cxa_throw -- exceptions disabled,
 * but the linker may still reference these if a TU sneaks a throw past
 * the compiler. */
void *__cxa_allocate_exception(size_t) noexcept { return nullptr; }
void  __cxa_throw(void *, void *, void (*)(void *)) {}

/* ------------------------------------------------------------------ */
/*  .init_array / .fini_array constructors (linker-defined)            */
/* ------------------------------------------------------------------ */

extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
extern void (*__fini_array_start[])(void);
extern void (*__fini_array_end[])(void);

void __libc_init_array(void)
{
    u64 count = (u64)(__init_array_end - __init_array_start);
    for (u64 i = 0; i < count; i++)
        __init_array_start[i]();
}

void __libc_fini_array(void)
{
    u64 count = (u64)(__fini_array_end - __fini_array_start);
    for (u64 i = count; i > 0; i--)
        __fini_array_start[i - 1]();
}

} /* extern "C" */
