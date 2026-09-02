#include "syscall.h"
#include "init_ram_getty.h"
#include "idt.h"
#include "stdio.h"
#include "string.h"
#include "tsc.h"
#include "klibc.h"

void syscall_init(void)
{
    idt_set_gate(0x80, (u64)syscall_handler, 0x08, 0xEF);
}

u32 syscall_dispatch(u32 num, u64 a1, u64 a2, u64 a3)
{
    switch (num) {
    case SYS_EXIT:
        task_exit(a1);
        return 0;
    case SYS_GETPID:
        return g_cur_task ? g_cur_task->pid : 0;
    case SYS_GETPPID:
        return g_cur_task && g_cur_task->pid > 0 ? g_cur_task->pid - 1 : 0;
    case SYS_WRITE:
        return (u32)fs_write(g_cur_task, (int)a1, (const void *)(uintptr_t)a2, (u32)a3);
    case SYS_READ:
        return (u32)fs_read(g_cur_task, (int)a1, (void *)(uintptr_t)a2, (u32)a3);
    case SYS_SLEEP: {
        uint64_t target = tsc_ms() + (uint64_t)a1;
        while (tsc_ms() < target)
            asm volatile ("pause");
        return 0;
    }
    case SYS_OPEN:
        return (u32)fs_open(g_cur_task, (const char *)(uintptr_t)a1, (u32)a2);
    case SYS_CLOSE:
        return (u32)fs_close(g_cur_task, (int)a1);
    case SYS_LSEEK:
        return (u32)fs_lseek(g_cur_task, (int)a1, (u32)a2, (u32)a3);
    case SYS_STAT:
        return (u32)fs_stat(g_cur_task, (const char *)(uintptr_t)a1,
                            (struct xkern_stat *)(uintptr_t)a2);
    case SYS_ACCESS:
        return (u32)fs_access((const char *)(uintptr_t)a1, (u32)a2);
    case SYS_GETDENTS:
        return (u32)fs_getdents((const char *)(uintptr_t)a1, (u32)a2,
                                (char *)(uintptr_t)a3, 256);
    case SYS_UNAME: {
        struct xkern_utsname *u = (struct xkern_utsname *)(uintptr_t)a1;

        if (!u)
            return (u32)-1;
        klibc.strcpy(u->sysname, "XKERN");
        klibc.strcpy(u->nodename, "xkern");
        klibc.strcpy(u->release, "0.1");
        klibc.strcpy(u->version, "XKERN 0.1");
        klibc.strcpy(u->machine, "x86_64");
        return 0;
    }
    case SYS_GETTIME:
        return (u32)tsc_ms();
    case SYS_BRK:
        return 0;
    case SYS_GETUID:
        return 0;
    case SYS_GETGID:
        return 0;
    case SYS_SPAWN:
        return spawn_task((const char *)(uintptr_t)a1,
                          (const char *)(uintptr_t)a2) == 0 ? 0 : (u32)-1;
    case SYS_VFORK:
        return task_vfork();
    case SYS_EXEC:
        return task_exec((const char *)(uintptr_t)a1,
                         (const char *)(uintptr_t)a2) == 0
                   ? XKERN_EXEC_MAGIC : (u32)-1;
    case SYS_WAIT:
        return task_wait(a1) == 0 ? 0 : (u32)-1;
    }
    return (u32)-1;
}
