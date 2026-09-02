#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"

#define SYS_EXIT      0
#define SYS_GETPID    1
#define SYS_WRITE     2
#define SYS_SLEEP     3
#define SYS_READ      4
#define SYS_SPAWN     5
#define SYS_OPEN      6
#define SYS_CLOSE     7
#define SYS_LSEEK     8
#define SYS_STAT      9
#define SYS_ACCESS    10
#define SYS_GETDENTS  11
#define SYS_UNAME     12
#define SYS_GETTIME   13
#define SYS_BRK       14
#define SYS_GETUID    15
#define SYS_GETGID    16
#define SYS_GETPPID   17
#define SYS_VFORK     18
#define SYS_EXEC      19
#define SYS_WAIT      20

/* Returned by a successful SYS_EXEC: the asm syscall epilogue recognises it
 * and switches straight into the newly loaded program. */
#define XKERN_EXEC_MAGIC 0x55550001

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define XKERN_STAT_SIZE 52

#define XKERN_DT_UNKNOWN 0
#define XKERN_DT_REG     1
#define XKERN_DT_DIR     2

struct xkern_stat {
    u32 st_dev;
    u32 st_ino;
    u32 st_mode;
    u32 st_nlink;
    u32 st_uid;
    u32 st_gid;
    u32 st_rdev;
    u32 st_size;
    u32 st_blksize;
    u32 st_blocks;
    u32 st_atime;
    u32 st_mtime;
    u32 st_ctime;
};

struct xkern_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

/*
 * x86_64 syscall ABI: int $0x80 with
 *   rax = number, rdi = a1, rsi = a2, rdx = a3; return value in rax.
 */
static inline u32 syscall(u32 num, u64 a1, u64 a2, u64 a3)
{
    u32 ret;

    asm volatile ("int $0x80"
                  : "=a"(ret)
                  : "a"((u64)num), "D"(a1), "S"(a2), "d"(a3)
                  : "memory");
    return ret;
}

static inline u32 sys_exit(u32 status)
{
    return syscall(SYS_EXIT, status, 0, 0);
}

static inline u32 sys_getpid(void)
{
    return syscall(SYS_GETPID, 0, 0, 0);
}

static inline u32 sys_getppid(void)
{
    return syscall(SYS_GETPPID, 0, 0, 0);
}

static inline u32 sys_write(u32 fd, const void *buf, u32 len)
{
    return syscall(SYS_WRITE, fd, (u64)buf, len);
}

static inline u32 sys_read(u32 fd, void *buf, u32 size)
{
    return syscall(SYS_READ, fd, (u64)buf, size);
}

static inline u32 sys_sleep(u32 ms)
{
    return syscall(SYS_SLEEP, ms, 0, 0);
}

static inline u32 sys_spawn(const char *path, const char *argline)
{
    return syscall(SYS_SPAWN, (u64)path, (u64)argline, 0);
}

static inline u32 sys_open(const char *path, u32 flags)
{
    return syscall(SYS_OPEN, (u64)path, flags, 0);
}

static inline u32 sys_close(u32 fd)
{
    return syscall(SYS_CLOSE, fd, 0, 0);
}

static inline u32 sys_lseek(u32 fd, u32 off, u32 whence)
{
    return syscall(SYS_LSEEK, fd, off, whence);
}

static inline u32 sys_stat(const char *path, void *st)
{
    return syscall(SYS_STAT, (u64)path, (u64)st, 0);
}

static inline u32 sys_access(const char *path, u32 mode)
{
    return syscall(SYS_ACCESS, (u64)path, mode, 0);
}

static inline u32 sys_getdents(const char *path, u32 index, char *name)
{
    return syscall(SYS_GETDENTS, (u64)path, index, (u64)name);
}

static inline u32 sys_uname(void *buf)
{
    return syscall(SYS_UNAME, (u64)buf, 0, 0);
}

static inline u32 sys_gettime(void)
{
    return syscall(SYS_GETTIME, 0, 0, 0);
}

static inline u32 sys_brk(u64 addr)
{
    return syscall(SYS_BRK, addr, 0, 0);
}

static inline u32 sys_getuid(void)
{
    return syscall(SYS_GETUID, 0, 0, 0);
}

static inline u32 sys_getgid(void)
{
    return syscall(SYS_GETGID, 0, 0, 0);
}

void syscall_init(void);
u32 syscall_dispatch(u32 num, u64 a1, u64 a2, u64 a3);

#endif
