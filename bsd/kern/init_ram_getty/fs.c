#include "init_ram_getty.h"
#include "syscall.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "klog.h"
#include "paging.h"
#include "vga.h"
#include "klibc.h"

#define FS_MAX_ARGS 32
#define FS_MAX_TOK  255
#define PAGE_SIZE   4096

static int parse_pid(const char *s, const char **end)
{
    int v = 0;
    int any = 0;

    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
        any = 1;
    }
    *end = s;
    return any ? v : -1;
}

#define S_IFMT   0xF000
#define S_IFDIR  0x4000
#define S_IFREG  0x8000
#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRGRP  0040
#define S_IROTH  0004

static void console_write(const void *buf, u32 n)
{
    const u8 *p = (const u8 *)buf;
    u32 i;

    for (i = 0; i < n; i++)
        vga_putchar((char)p[i]);
}

static void console_read(void *buf, u32 n)
{
    (void)buf;
    (void)n;
}

void fs_task_init(struct task *t)
{
    int i;

    for (i = 0; i < XKERN_MAX_FDS; i++) {
        t->fd[i].type = 0;
        t->fd[i].data = 0;
        t->fd[i].size = 0;
        t->fd[i].pos = 0;
    }
    t->fd[0].type = 2;
    t->fd[1].type = 2;
    t->fd[2].type = 2;
}

static int fs_alloc_fd(struct task *t)
{
    int i;

    for (i = 3; i < XKERN_MAX_FDS; i++) {
        if (t->fd[i].type == 0)
            return i;
    }
    return -1;
}

static int path_is_dir(const char *path)
{
    const char *p = path;

    while (*p == '/')
        p++;
    if (*p == '\0')
        return 1;
    return 0;
}

static int is_procfs(const char *path)
{
    const char *p = path;

    while (*p == '/')
        p++;
    return klibc.strncmp(p, "proc", 4) == 0 &&
           (p[4] == '\0' || p[4] == '/');
}

static int procfs_fill(struct task *t, const char *path)
{
    return procfs_read(path, t->procbuf, 1024);
}

static int procfs_is_file(const char *path)
{
    const char *p = path;
    const char *end;
    int pid;

    while (*p == '/')
        p++;
    p += 4;
    if (*p != '/')
        return 0;
    p++;
    pid = parse_pid(p, &end);
    if (pid < 0 || *end != '/')
        return 0;
    if (pid >= PROC_MAX_TASKS)
        return 0;
    p = end + 1;
    return klibc.strcmp(p, "status") == 0;
}

static int procfs_exists(const char *path)
{
    const char *p = path;
    const char *end;
    int pid;

    while (*p == '/')
        p++;
    if (klibc.strncmp(p, "proc", 4) != 0)
        return 0;
    p += 4;
    if (*p == '\0')
        return 1;
    if (*p != '/')
        return 0;
    p++;
    if (*p == '\0')
        return 1;
    pid = parse_pid(p, &end);
    if (pid < 0)
        return 0;
    if (pid >= PROC_MAX_TASKS)
        return 0;
    if (*end == '\0')
        return 1;
    if (*end != '/')
        return 0;
    p = end + 1;
    return klibc.strcmp(p, "status") == 0;
}

static int procfs_child(struct task *t, const char *path, u32 index,
                        char *name, u32 namelen)
{
    const char *p = path;
    const char *end;
    u32 count = 0;

    (void)t;
    while (*p == '/')
        p++;
    p += 4;
    if (*p == '\0' || *p == '/') {
        int i;

        if (*p == '/')
            p++;
        if (*p == '\0') {
            /* /proc: children are pids */
            for (i = 0; i < task_count(); i++) {
                struct task *t = task_at(i);

                if (count++ == index) {
                    char tmp[16];
                    int n = klibc.snprintf(tmp, sizeof(tmp), "%u", t->pid);
                    if ((u32)n >= namelen)
                        n = (int)namelen - 1;
                    klibc.memcpy(name, tmp, (u32)n);
                    name[n] = '\0';
                    return XKERN_DT_DIR;
                }
            }
            return 0;
        }
        /* /proc/N */
        (void)parse_pid(p, &end);
        if (end == p || *end != '/')
            return 0;
        p = end + 1;
        if (klibc.strcmp(p, "status") != 0)
            return 0;
        if (index == 0) {
            if (namelen < 7)
                return 0;
            klibc.memcpy(name, "status", 7);
            return XKERN_DT_REG;
        }
        return 0;
    }
    return 0;
}

int fs_open(struct task *t, const char *path, u32 flags)
{
    struct ramfs_file *f;
    int fd;
    int n;

    (void)flags;

    if (is_procfs(path)) {
        n = procfs_fill(t, path);
        if (n < 0)
            return -1;
        fd = fs_alloc_fd(t);
        if (fd < 0)
            return -1;
        t->fd[fd].type = 1;
        t->fd[fd].data = (const u8 *)t->procbuf;
        t->fd[fd].size = (u32)n;
        t->fd[fd].pos = 0;
        return fd;
    }

    f = ramfs_lookup(ramfs_get(), path);
    if (!f)
        return -1;

    fd = fs_alloc_fd(t);
    if (fd < 0)
        return -1;

    t->fd[fd].type = 1;
    t->fd[fd].data = f->data;
    t->fd[fd].size = f->size;
    t->fd[fd].pos = 0;
    return fd;
}

int fs_close(struct task *t, int fd)
{
    if (fd < 0 || fd >= XKERN_MAX_FDS)
        return -1;
    if (fd < 3)
        return 0;
    t->fd[fd].type = 0;
    return 0;
}

int fs_read(struct task *t, int fd, void *buf, u32 n)
{
    u32 avail;

    if (fd < 0 || fd >= XKERN_MAX_FDS)
        return -1;

    if (t->fd[fd].type == 2) {
        console_read(buf, n);
        return 0;
    }
    if (t->fd[fd].type != 1)
        return -1;

    if (t->fd[fd].pos >= t->fd[fd].size)
        return 0;
    avail = t->fd[fd].size - t->fd[fd].pos;
    if (n > avail)
        n = avail;
    klibc.memcpy(buf, t->fd[fd].data + t->fd[fd].pos, n);
    t->fd[fd].pos += n;
    return (int)n;
}

int fs_write(struct task *t, int fd, const void *buf, u32 n)
{
    if (fd < 0 || fd >= XKERN_MAX_FDS)
        return -1;

    if (t->fd[fd].type == 2) {
        console_write(buf, n);
        return (int)n;
    }
    if (t->fd[fd].type != 1)
        return -1;
    return -1;
}

int fs_lseek(struct task *t, int fd, u32 off, u32 whence)
{
    struct xkern_fd *f;

    if (fd < 0 || fd >= XKERN_MAX_FDS)
        return -1;
    f = &t->fd[fd];
    if (f->type != 1)
        return -1;

    switch (whence) {
    case SEEK_SET:
        f->pos = off;
        break;
    case SEEK_CUR:
        f->pos += off;
        break;
    case SEEK_END:
        f->pos = f->size + off;
        break;
    default:
        return -1;
    }
    return (int)f->pos;
}

static u32 stat_mode(const char *path)
{
    if (path_is_dir(path))
        return S_IFDIR | 0755;
    return S_IFREG | (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
}

static int ramfs_dir_exists(const char *path)
{
    char prefix[RAMFS_NAME_MAX + 1];
    u32 plen;
    int i;

    if (path_is_dir(path))
        return 1;
    if (is_procfs(path))
        return !procfs_is_file(path);

    while (*path == '/')
        path++;
    plen = (u32)klibc.strlen(path);
    if (plen == 0)
        return 1;
    if (plen >= RAMFS_NAME_MAX)
        return 0;
    klibc.memcpy(prefix, path, plen);
    prefix[plen] = '/';
    prefix[plen + 1] = '\0';
    for (i = 0; i < ramfs_get()->count; i++) {
        if (klibc.strncmp(ramfs_get()->files[i].name, prefix, plen + 1) == 0)
            return 1;
    }
    return 0;
}

static int ramfs_stat(struct task *t, const char *path, struct xkern_stat *st)
{
    struct ramfs_file *f;
    int i;

    klibc.memset(st, 0, sizeof(*st));
    st->st_nlink = 1;
    st->st_blksize = 4096;

    if (ramfs_dir_exists(path)) {
        st->st_mode = S_IFDIR | 0755;
        st->st_size = 0;
        return 0;
    }

    if (is_procfs(path)) {
        int n = procfs_fill(t, path);

        if (n < 0)
            return -1;
        st->st_mode = stat_mode(path);
        st->st_ino = 0;
        st->st_size = (u32)n;
        st->st_blocks = ((u32)n + 511) / 512;
        return 0;
    }

    f = ramfs_lookup(ramfs_get(), path);
    if (!f)
        return -1;

    for (i = 0; i < ramfs_get()->count; i++) {
        if (&ramfs_get()->files[i] == f)
            break;
    }
    st->st_mode = stat_mode(path);
    st->st_ino = (u32)i + 1;
    st->st_size = f->size;
    st->st_blocks = (f->size + 511) / 512;
    (void)t;
    return 0;
}

int fs_stat(struct task *t, const char *path, struct xkern_stat *st)
{
    return ramfs_stat(t, path, st);
}

int fs_fstat(struct task *t, int fd, struct xkern_stat *st)
{
    struct xkern_fd *f;
    char path[RAMFS_NAME_MAX];
    int i;

    if (fd < 0 || fd >= XKERN_MAX_FDS)
        return -1;
    f = &t->fd[fd];
    if (f->type == 2) {
        klibc.memset(st, 0, sizeof(*st));
        st->st_mode = S_IFREG | 0600;
        return 0;
    }
    if (f->type != 1)
        return -1;

    for (i = 0; i < ramfs_get()->count; i++) {
        struct ramfs_file *rf = &ramfs_get()->files[i];

        if (rf->data == f->data && rf->size == f->size) {
            path[0] = '/';
            klibc.strcpy(path + 1, rf->name);
            return ramfs_stat(t, path, st);
        }
    }
    return -1;
}

int fs_access(const char *path, u32 mode)
{
    (void)mode;

    if (ramfs_dir_exists(path))
        return 0;
    if (is_procfs(path))
        return procfs_exists(path) ? 0 : -1;
    if (ramfs_lookup(ramfs_get(), path))
        return 0;
    return -1;
}

/* Child-name enumeration for flat ramfs: returns the (index+1)-th unique
 * first path component under dir.  Returns 0 at end, 1 = file, 2 = dir. */
static int dir_child(struct ramfs *fs, const char *dir, u32 index,
                     char *name, u32 namelen)
{
    u32 seen = 0;
    u32 want = index + 1;
    char prefix[RAMFS_NAME_MAX + 2];
    u32 plen = 0;
    int i;

    while (*dir == '/')
        dir++;

    if (*dir != '\0') {
        plen = (u32)klibc.strlen(dir);
        if (plen >= RAMFS_NAME_MAX)
            return 0;
        klibc.memcpy(prefix, dir, plen);
        prefix[plen] = '/';
        plen++;
        prefix[plen] = '\0';
    }

    for (i = 0; i < fs->count; i++) {
        const char *n = fs->files[i].name;
        const char *child;
        const char *slash;
        u32 len;
        int j, dup = 0;

        if (plen) {
            if (klibc.strncmp(n, prefix, plen) != 0)
                continue;
            child = n + plen;
        } else {
            child = n;
        }
        if (!*child)
            continue;

        slash = klibc.strchr(child, '/');
        if (slash) {
            len = (u32)(slash - child);
        } else {
            len = (u32)klibc.strlen(child);
        }

        for (j = 0; j < i; j++) {
            const char *m = fs->files[j].name;
            const char *cm, *sm;
            u32 lm;

            if (plen) {
                if (klibc.strncmp(m, prefix, plen) != 0)
                    continue;
                cm = m + plen;
            } else {
                cm = m;
            }
            sm = klibc.strchr(cm, '/');
            lm = sm ? (u32)(sm - cm) : (u32)klibc.strlen(cm);
            if (lm == len && klibc.strncmp(cm, child, len) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup)
            continue;

        seen++;
        if (seen == want) {
            if (len >= namelen)
                len = namelen - 1;
            klibc.memcpy(name, child, len);
            name[len] = '\0';
            return slash ? XKERN_DT_DIR : XKERN_DT_REG;
        }
    }
    return 0;
}

int fs_getdents(const char *path, u32 index, char *name, u32 namelen)
{
    if (is_procfs(path))
        return procfs_child(g_cur_task, path, index, name, namelen);
    return dir_child(ramfs_get(), path, index, name, namelen);
}

#define ARGV_BASE 0x08100000ull

/* Tokenize a kernel-space command line into argv strings copied into the
 * task's address space.  Returns the virtual address of argv[] (or 0). */
u64 task_build_argv(struct task *t, const char *argline, u64 *argc_out)
{
    char toks[FS_MAX_ARGS][FS_MAX_TOK];
    char *tp[FS_MAX_ARGS];
    u64 argc = 0;
    u32 i;
    u64 need, page;
    u64 vaddr = ARGV_BASE;
    u64 old_cr3;
    int in_single = 0, in_double = 0;
    u32 wlen = 0;

    if (argline) {
        const char *p = argline;

        while (*p && argc < FS_MAX_ARGS) {
            while (*p == ' ' || *p == '\t' || *p == '\n')
                p++;
            if (!*p)
                break;
            wlen = 0;
            in_single = in_double = 0;
            while (*p && (in_single || in_double ||
                          !(*p == ' ' || *p == '\t' || *p == '\n'))) {
                char c = *p++;
                if (in_single) {
                    if (c == '\'') {
                        in_single = 0;
                        continue;
                    }
                } else if (in_double) {
                    if (c == '"') {
                        in_double = 0;
                        continue;
                    }
                } else if (c == '\'') {
                    in_single = 1;
                    continue;
                } else if (c == '"') {
                    in_double = 1;
                    continue;
                }
                if (wlen < FS_MAX_TOK - 1)
                    toks[argc][wlen++] = c;
            }
            toks[argc][wlen] = '\0';
            tp[argc] = toks[argc];
            argc++;
        }
    }

    if (argc == 0) {
        toks[0][0] = '/';
        klibc.strcpy(toks[0] + 1, t->name);
        tp[0] = toks[0];
        argc = 1;
    }

    need = (argc + 1) * sizeof(u64);
    for (i = 0; i < argc; i++)
        need += (u64)klibc.strlen(tp[i]) + 1;

    if (need > 0x00100000ull)
        return 0;

    old_cr3 = paging_cr3();
    asm volatile ("movq %0, %%cr3" : : "r"(t->cr3) : "memory");

    for (page = vaddr & ~(PAGE_SIZE - 1);
         page < vaddr + need; page += PAGE_SIZE) {
        if (!paging_alloc_and_map(page, PAGE_WRITE)) {
            asm volatile ("movq %0, %%cr3" : : "r"(old_cr3) : "memory");
            return 0;
        }
    }

    {
        u64 *arr = (u64 *)vaddr;
        char *s = (char *)(vaddr + (argc + 1) * sizeof(u64));

        for (i = 0; i < argc; i++) {
            u32 l = (u32)klibc.strlen(tp[i]) + 1;

            arr[i] = (u64)s;
            klibc.memcpy(s, tp[i], l);
            s += l;
        }
        arr[argc] = 0;
    }

    asm volatile ("movq %0, %%cr3" : : "r"(old_cr3) : "memory");

    if (argc_out)
        *argc_out = argc;
    return vaddr;
}
