#include "init_ram_getty.h"
#include "syscall.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include "panic.h"
#include "paging.h"
#include "elf.h"
#include "klibc.h"

static struct task g_tasks[PROC_MAX_TASKS];
static u8 g_task_stacks[PROC_MAX_TASKS][TASK_STACK_SIZE]
    __attribute__((aligned(16)));
static int g_task_count;

struct task *g_cur_task;

/* Saved register frame of the in-flight syscall (see isr.asm and the
 * FRAME_* indices in init_ram_getty.h). */
u64 g_syscall_frame;

void sched_tick(void)
{
    int slot;
    int i;

    if (g_task_count <= 0 || !g_cur_task)
        panic("sched: no task to schedule");

    slot = (int)(g_cur_task - g_tasks);
    g_cur_task->ticks++;

    for (i = 1; i < g_task_count; i++) {
        struct task *t;
        int s = slot + i;

        if (s >= g_task_count)
            s -= g_task_count;
        t = &g_tasks[s];
        if (t->state == TASK_RUNNING) {
            g_cur_task = t;
            return;
        }
    }

    if (g_cur_task->state != TASK_RUNNING)
        panic("sched: no runnable task");
}

struct task *task_alloc(const char *name, void *arg)
{
    struct task *t;
    u64 cr3;

    if (g_task_count >= PROC_MAX_TASKS)
        return 0;

    cr3 = paging_clone_pd();
    if (!cr3)
        return 0;

    t = &g_tasks[g_task_count];
    t->pid = (u32)g_task_count;
    klibc.strncpy(t->name, name, TASK_NAME_MAX - 1);
    t->name[TASK_NAME_MAX - 1] = '\0';
    t->stack = (u64)g_task_stacks[g_task_count];
    t->cr3 = cr3;
    t->sp = 0;
    t->state = TASK_NEW;
    t->ticks = 0;
    t->arg = arg;
    fs_task_init(t);

    g_task_count++;
    //currently hiddden
    //klog("proc", "task %u '%s' created", t->pid, t->name);
    return t;
}

/*
 * Build an initial interrupt frame on the task's kernel stack so that
 * sched_start -> irq0_resume lands inside the task.  Layout must mirror
 * PUSHALL/POPALL in osfmk/x86_64/isr.asm:
 *
 *   [irq0_resume]                <- returned into by sched_switch.resume
 *   r15 r14 r13 r12 r11 r10 r9 r8 rdi(argc) rsi(argv) rbp rbx rdx rcx rax
 *   [rip][cs][rflags][rsp][ss]   <- consumed by iretq
 */
void task_set_entry(struct task *t, void (*entry)(void), u64 argc, u64 argv)
{
    u64 *s;

    if (!t)
        return;

    s = (u64 *)(t->stack + TASK_STACK_SIZE);

    /* CPU frame for iretq */
    *--s = 0x10;                    /* ss */
    *--s = t->stack + TASK_STACK_SIZE; /* rsp */
    *--s = 0x202;                   /* rflags */
    *--s = 0x08;                    /* cs */
    *--s = (u64)entry;              /* rip */

    /* GP registers (PUSHALL order reversed): rax rcx rdx rbx rbp rsi rdi r8-r15 */
    *--s = 0;                       /* rax */
    *--s = 0;                       /* rcx */
    *--s = 0;                       /* rdx */
    *--s = 0;                       /* rbx */
    *--s = 0;                       /* rbp */
    *--s = argv;                    /* rsi: argv */
    *--s = argc;                    /* rdi: argc */
    *--s = 0;                       /* r8  */
    *--s = 0;                       /* r9  */
    *--s = 0;                       /* r10 */
    *--s = 0;                       /* r11 */
    *--s = 0;                       /* r12 */
    *--s = 0;                       /* r13 */
    *--s = 0;                       /* r14 */
    *--s = 0;                       /* r15 */

    *--s = (u64)irq0_resume;        /* resume trampoline return address */
    t->sp = (u64)s;
    t->state = TASK_RUNNING;
}

struct task *task_create(const char *name, void (*entry)(void), void *arg)
{
    struct task *t = task_alloc(name, arg);

    if (t)
        task_set_entry(t, entry, 0, 0);
    return t;
}

void task_exit(u32 status)
{
    struct task *p;

    g_cur_task->state = TASK_ZOMBIE;
    (void)status;
    //currently hidden
    //klog("proc", "task %u '%s' exited", g_cur_task->pid, g_cur_task->name);

    if (g_cur_task->vfork_parent) {
        p = task_find(g_cur_task->vfork_parent);
        if (p && p->state == TASK_WAITING)
            p->state = TASK_RUNNING;
    }
    for (;;)
        asm volatile ("hlt");
}

u32 task_vfork(void)
{
    struct task *p = g_cur_task;
    struct task *t;
    u64 cr3;
    u64 *f = (u64 *)g_syscall_frame;
    u64 *s;

    if (g_task_count >= PROC_MAX_TASKS)
        return (u32)-1;

    cr3 = paging_vfork_clone();
    if (!cr3) {
        klog("proc", "vfork: clone pd failed");
        return (u32)-1;
    }

    t = &g_tasks[g_task_count];
    t->pid = (u32)g_task_count;
    klibc.strncpy(t->name, p->name, TASK_NAME_MAX - 1);
    t->name[TASK_NAME_MAX - 1] = '\0';
    t->stack = (u64)g_task_stacks[g_task_count];
    t->cr3 = cr3;
    t->sp = 0;
    t->state = TASK_NEW;
    t->ticks = 0;
    t->arg = 0;
    t->vfork_parent = p->pid;
    klibc.memcpy(t->fd, p->fd, sizeof(t->fd));
    klibc.memcpy(t->procbuf, p->procbuf, sizeof(t->procbuf));

    g_task_count++;
    klog("proc", "task %u vfork child of %u created", t->pid, p->pid);

    /* Child resumes right after the int $0x80 in the vfork wrapper with
     * rax = 0, on the parent's shared stack. */
    s = (u64 *)(t->stack + TASK_STACK_SIZE);

    *--s = f[FRAME_SS];
    *--s = f[FRAME_RSP];            /* shared parent stack */
    *--s = f[FRAME_RFLAGS];
    *--s = f[FRAME_CS];
    *--s = f[FRAME_RIP];

    *--s = 0;                       /* rax: child return value */
    *--s = f[13];                   /* rcx */
    *--s = f[FRAME_RDX];            /* rdx */
    *--s = f[11];                   /* rbx */
    *--s = f[10];                   /* rbp */
    *--s = f[FRAME_RSI];            /* rsi */
    *--s = f[FRAME_RDI];            /* rdi */
    *--s = f[7];                    /* r8 */
    *--s = f[6];                    /* r9 */
    *--s = f[5];                    /* r10 */
    *--s = f[4];                    /* r11 */
    *--s = f[3];                    /* r12 */
    *--s = f[2];                    /* r13 */
    *--s = f[1];                    /* r14 */
    *--s = f[0];                    /* r15 */

    *--s = (u64)irq0_resume;
    t->sp = (u64)s;
    t->state = TASK_RUNNING;

    /* vfork: parent must not run while the child shares its address space. */
    p->state = TASK_WAITING;

    return t->pid;
}

int task_exec(const char *path, const char *argline)
{
    struct task *t = g_cur_task;
    struct ramfs_file *f = ramfs_lookup(ramfs_get(), path);
    u64 entry;
    u64 new_cr3, old_cr3;
    u64 argc, argv;

    if (!f)
        return -1;
    if (!elf_valid(f->data, f->size))
        return -1;

    new_cr3 = paging_clone_pd();
    if (!new_cr3)
        return -1;
    if (elf_load_pd(new_cr3, f->data, f->size, &entry, 0, 0) != 0)
        return -1;

    old_cr3 = t->cr3;
    t->cr3 = new_cr3;
    t->stack = (u64)g_task_stacks[t->pid];
    argv = task_build_argv(t, argline, &argc);
    if (!argv) {
        t->cr3 = old_cr3;
        return -1;
    }
    task_set_entry(t, (void (*)(void))entry, argc, argv);

    if (t->vfork_parent) {
        struct task *p = task_find(t->vfork_parent);

        if (p && p->state == TASK_WAITING)
            p->state = TASK_RUNNING;
    }

  //  klog("proc", "task %u exec -> '%s' entry %x", t->pid, path, entry);
    return 0;
}

int task_wait(u32 pid)
{
    struct task *child;

    for (;;) {
        child = task_find(pid);
        if (!child)
            return -1;
        if (child->state == TASK_ZOMBIE)
            return 0;
        g_cur_task->state = TASK_WAITING;
        sched_switch();
        g_cur_task->state = TASK_RUNNING;
    }
}

struct task *task_find(u32 pid)
{
    if (pid < (u32)g_task_count)
        return &g_tasks[pid];
    return 0;
}

int task_count(void)
{
    return g_task_count;
}

struct task *task_at(int i)
{
    if (i < 0 || i >= g_task_count)
        return 0;
    return &g_tasks[i];
}

int procfs_read(const char *path, char *buf, u32 bufsize)
{
    const char *p = path;

    while (*p == '/')
        p++;

    if (klibc.strcmp(p, "proc/1/status") == 0) {
        struct task *t = task_find(1);
        if (!t)
            return -1;
      //  snprintf(buf, bufsize,
     //            "pid: %u\nname: %s\nstate: %s\nticks: %u\n",
     //            t->pid, t->name,
     //            t->state == TASK_RUNNING ? "RUNNING" : "ZOMBIE",
     //            t->ticks);
        return (int)klibc.strlen(buf);
    }

    return -1;
}

int procfs_list(char *buf, u32 bufsize)
{
    int i;
    u32 pos = 0;

    for (i = 0; i < g_task_count; i++) {
        char tmp[48];
        int n = klibc.snprintf(tmp, sizeof(tmp), "%u %s\n",
                         g_tasks[i].pid, g_tasks[i].name);
        if (pos + (u32)n < bufsize) {
            klibc.memcpy(buf + pos, tmp, (u32)n);
            pos += (u32)n;
        }
    }
    buf[pos] = '\0';
    return (int)pos;
}

void sched_boot(struct task *idle)
{
    if (!idle || g_task_count <= 0)
        panic("sched: no task to boot");
    g_cur_task = idle;
    sched_start();
}
