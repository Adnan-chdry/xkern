#ifndef INIT_RAM_GETTY_H
#define INIT_RAM_GETTY_H

#include "types.h"
#include "multiboot.h"
#include "syscall.h"
#include "ramfs_compat.h"

#define INIT_LINE_MAX   256
#define INIT_TOKENS_MAX 16

#define TASK_NAME_MAX   32
#define TASK_STACK_SIZE 8192
#define PROC_MAX_TASKS  32
#define XKERN_MAX_FDS   16

#define TASK_RUNNING 0
#define TASK_ZOMBIE  1
#define TASK_NEW     2
#define TASK_WAITING 3

struct xkern_fd {
    u32 type;       /* 0 = free, 1 = ramfs file, 2 = console */
    const u8 *data;
    u32 size;
    u32 pos;
};

enum init_cmd {
    CMD_UNKNOWN = 0,
    CMD_ECHO,
    CMD_CLEAR,
    CMD_VERSION,
    CMD_LIST,
    CMD_READ,
    CMD_PID,
    CMD_SLEEP,
    CMD_SPAWN,
    CMD_SHELL,
    CMD_IDLE,
    CMD_EXIT,
};

/*
 * Canonical interrupt/syscall register frame (see osfmk/x86_64/isr.asm).
 * Qword indices from the saved-frame pointer g_syscall_frame:
 *   f[0]=r15 f[1]=r14 f[2]=r13 f[3]=r12 f[4]=r11 f[5]=r10 f[6]=r9 f[7]=r8
 *   f[8]=rdi f[9]=rsi f[10]=rbp f[11]=rbx f[12]=rdx f[13]=rcx f[14]=rax
 *   f[15]=rip f[16]=cs f[17]=rflags f[18]=rsp f[19]=ss
 */
#define FRAME_RDI    8
#define FRAME_RSI    9
#define FRAME_RDX    12
#define FRAME_RAX    14
#define FRAME_RIP    15
#define FRAME_CS     16
#define FRAME_RFLAGS 17
#define FRAME_RSP    18
#define FRAME_SS     19

struct task {
    u64  sp;       /* first member: sched_switch asm reads/writes offset 0 */
    u64  cr3;      /* second member: sched_switch asm switches CR3 at +8   */
    u32  pid;
    char name[TASK_NAME_MAX];
    u64  stack;
    int  state;
    int  ticks;
    void *arg;
    u32  vfork_parent;   /* pid of parent waiting on this vfork child (0 = none) */
    struct xkern_fd fd[XKERN_MAX_FDS];
    char procbuf[1024];
};

struct script_task_data {
    char path[RAMFS_NAME_MAX];
};

/* init.c */
int  initram_collect(struct multiboot_info *mbi, struct ramfs *fs);
void initram_reserve(void);
int  initram_getty_init(struct ramfs *fs);

/* script.c */
void script_set_fs(struct ramfs *fs);
struct script_task_data *script_task_data_new(void);
int  script_tokenize(char *line, char *argv[]);
int  script_exec(struct ramfs *fs, int argc, char *argv[]);
int  script_run(struct ramfs *fs, const char *path);
void script_shell(struct ramfs *fs);
void script_task_main(void);
int  spawn_task(const char *path, const char *argline);

/* fs.c */
void fs_task_init(struct task *t);
int  fs_open(struct task *t, const char *path, u32 flags);
int  fs_close(struct task *t, int fd);
int  fs_read(struct task *t, int fd, void *buf, u32 n);
int  fs_write(struct task *t, int fd, const void *buf, u32 n);
int  fs_lseek(struct task *t, int fd, u32 off, u32 whence);
int  fs_stat(struct task *t, const char *path, struct xkern_stat *st);
int  fs_fstat(struct task *t, int fd, struct xkern_stat *st);
int  fs_access(const char *path, u32 mode);
int  fs_getdents(const char *path, u32 index, char *name, u32 namelen);

/* valid.c */
int valid_hex_field(const char *s, int len);
int valid_cpio_name(const char *name, u32 namesize);
int valid_command(const char *name);
int valid_args(const char *name, int nargs);

/* proc.c */
extern struct task *g_cur_task;
extern void irq0_resume(void);
void sched_start(void);
void sched_switch(void);
void sched_boot(struct task *idle);
void sched_tick(void);
struct task *task_alloc(const char *name, void *arg);
void task_set_entry(struct task *t, void (*entry)(void), u64 argc, u64 argv);
struct task *task_create(const char *name, void (*entry)(void), void *arg);
void task_exit(u32 status);
struct task *task_find(u32 pid);
int task_count(void);
struct task *task_at(int i);
u32 task_vfork(void);
int task_exec(const char *path, const char *argline);
int task_wait(u32 pid);
u64 task_build_argv(struct task *t, const char *argline, u64 *argc_out);
int  procfs_read(const char *path, char *buf, u32 bufsize);
int  procfs_list(char *buf, u32 bufsize);

#endif
