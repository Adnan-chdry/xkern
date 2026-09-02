#include "init_ram_getty.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "vga.h"
#include "syscall.h"
#include "elf.h"
#include "klibc.h"
#include "klog.h"

#define CPIO_NEWC_HEADER 110
#define SCRIPT_DATA_MAX  8

static struct ramfs *g_fs;
static struct script_task_data g_script_data[SCRIPT_DATA_MAX];
static int g_script_data_count;

void script_set_fs(struct ramfs *fs)
{
    g_fs = fs;
}

struct script_task_data *script_task_data_new(void)
{
    struct script_task_data *d;

    if (g_script_data_count >= SCRIPT_DATA_MAX)
        return 0;

    d = &g_script_data[g_script_data_count++];
    d->path[0] = '\0';
    return d;
}

static int read_line(char *buf, int size)
{
    int i = 0;

    for (;;) {
        int c = klibc.getchar();
        if (c == '\n')
            break;
        if (c == '\b') {
            if (i > 0)
                i--;
            continue;
        }
        if (c < 32 || c > 126)
            continue;
        if (i < size - 1)
            buf[i++] = (char)c;
    }
    buf[i] = '\0';
    return i;
}

int script_tokenize(char *line, char *argv[])
{
    int argc = 0;

    while (*line) {
        while (*line == ' ' || *line == '\t')
            line++;
        if (!*line)
            break;
        if (argc < INIT_TOKENS_MAX - 1)
            argv[argc++] = line;
        while (*line && *line != ' ' && *line != '\t')
            line++;
        if (*line) {
            *line = '\0';
            line++;
        }
    }
    argv[argc] = 0;
    return argc;
}

static int cmd_spawn(const char *path)
{
    struct script_task_data *d = script_task_data_new();

    if (!d) {
        klog_lvl(KLOG_ERR, "launchd", "spawn: table full");
        return 0;
    }
    klibc.strncpy(d->path, path, RAMFS_NAME_MAX - 1);
    d->path[RAMFS_NAME_MAX - 1] = '\0';
    if (!task_create(path, script_task_main, d))
        klog_lvl(KLOG_ERR, "launchd", "spawn: task table full");
    return 0;
}

int spawn_task(const char *path, const char *argline)
{
    struct ramfs_file *f = ramfs_lookup(g_fs, path);
    u64 entry;

    if (!f) {
        klog_lvl(KLOG_ERR, "launchd", "init: '%s' not found", path);
        return -1;
    }

    if (elf_valid(f->data, f->size)) {
        struct task *t = task_alloc(path, 0);
        u64 argc, argv;

        if (!t) {
            klog_lvl(KLOG_ERR, "launchd", "init: task table full");
            return -1;
        }
        if (elf_load_pd(t->cr3, f->data, f->size, &entry, 0, 0) != 0) {
            t->state = TASK_ZOMBIE;
            klog_lvl(KLOG_ERR, "launchd", "init: '%s' is not a loadable ELF", path);
            return -1;
        }
        argv = task_build_argv(t, argline, &argc);
        task_set_entry(t, (void (*)(void))entry, argc, argv);
        return 0;
    }

    return cmd_spawn(path);
}

int script_exec(struct ramfs *fs, int argc, char *argv[])
{
    int id = valid_command(argv[0]);

    if (id == CMD_UNKNOWN || !valid_args(argv[0], argc - 1)) {
        klog_lvl(KLOG_WARNING, "launchd", "init: invalid command '%s'", argv[0]);
        return 0;
    }

    switch (id) {
    case CMD_ECHO: {
        char b[INIT_LINE_MAX];
        int pos = 0;
        int i;
        for (i = 1; i < argc; i++) {
            const char *a = argv[i];
            if (i > 1 && pos < (int)sizeof(b) - 1)
                b[pos++] = ' ';
            while (*a && pos < (int)sizeof(b) - 1)
                b[pos++] = *a++;
        }
        if (pos < (int)sizeof(b) - 1)
            b[pos++] = '\n';
        sys_write(1, b, (u32)pos);
        return 0;
    }
    case CMD_CLEAR:
        vga_clear();
        return 0;
    case CMD_VERSION:
        klibc.printf("XKERN initram getty v0.1\n");
        return 0;
    case CMD_LIST: {
        char b[128];
        if (argc > 1 && klibc.strcmp(argv[1], "/proc") == 0) {
            procfs_list(b, sizeof(b));
            klibc.printf("%s", b);
        } else {
            ramfs_list(fs);
        }
        return 0;
    }
    case CMD_READ: {
        char b[512];
        int fd;
        int n;

        fd = (int)sys_open(argv[1], 0);
        if (fd < 0) {
            klibc.printf("init: '%s' not found\n", argv[1]);
            return 0;
        }
        n = (int)sys_read((u32)fd, b, (u32)sizeof(b));
        sys_close((u32)fd);
        if (n > 0) {
            b[n] = '\0';
            klibc.printf("%s", b);
        }
        return 0;
    }
    case CMD_PID:
        klibc.printf("pid %u (%s)\n", sys_getpid(), g_cur_task->name);
        return 0;
    case CMD_SLEEP:
        sys_sleep((u32)klibc.atoi(argv[1]));
        return 0;
    case CMD_SPAWN: {
        u32 i;
        char argline[INIT_LINE_MAX];
        int pos = 0;

        argline[0] = '\0';
        for (i = 1; i < (u32)argc; i++) {
            const char *a = argv[i];
            if (pos > 0)
                argline[pos++] = ' ';
            while (*a && pos < INIT_LINE_MAX - 1)
                argline[pos++] = *a++;
        }
        argline[pos] = '\0';
        spawn_task(argv[1], argline);
        return 0;
    }
    case CMD_SHELL:
        script_shell(fs);
        return 0;
    case CMD_IDLE:
        for (;;)
            asm volatile ("sti; hlt");
        return 0;
    case CMD_EXIT:
        return 1;
    }
    return 0;
}

int script_run(struct ramfs *fs, const char *path)
{
    struct ramfs_file *f = ramfs_lookup(fs, path);
    char *buf, *line;

    if (!f) {
        klog_lvl(KLOG_ERR, "launchd", "init: '%s' not found", path);
        return -1;
    }

    buf = (char *)f->data;
    line = buf;

    while (line && *line) {
        char *argv[INIT_TOKENS_MAX];
        char *nl = klibc.strchr(line, '\n');
        int argc;

        if (nl)
            *nl = '\0';
        argc = script_tokenize(line, argv);
        if (argc > 0) {
            if (script_exec(fs, argc, argv) != 0)
                return 1;
        }
        if (!nl)
            break;
        line = nl + 1;
    }
    return 0;
}

void script_shell(struct ramfs *fs)
{
    char line[INIT_LINE_MAX];

    klibc.printf("getty shell on initram, type 'exit' to stop\n");

    for (;;) {
        char *argv[INIT_TOKENS_MAX];
        int argc;

        klibc.printf("$ ");
        read_line(line, sizeof(line));
        argc = script_tokenize(line, argv);
        if (argc > 0) {
            if (script_exec(fs, argc, argv) != 0)
                break;
        }
    }
}

void script_task_main(void)
{
    struct script_task_data *d = (struct script_task_data *)g_cur_task->arg;

    klibc.printf("[%u] %s: start\n", g_cur_task->pid, g_cur_task->name);
    script_run(g_fs, d->path);
    klibc.printf("[%u] %s: done, exiting\n", g_cur_task->pid, g_cur_task->name);
    sys_exit(0);
}
