#include "init_ram_getty.h"
#include "string.h"
#include "klibc.h"

struct cmd_spec {
    const char *name;
    int min_args;
    int max_args;
};

static const struct cmd_spec cmd_table[] = {
    { "echo",    1, INIT_TOKENS_MAX - 1 },
    { "clear",   0, 0 },
    { "version", 0, 0 },
    { "list",    0, 1 },
    { "read",    1, 1 },
    { "pid",     0, 0 },
    { "sleep",   1, 1 },
    { "spawn",   1, 1 },
    { "shell",   0, 0 },
    { "idle",    0, 0 },
    { "exit",    0, 0 },
};

int valid_hex_field(const char *s, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

int valid_cpio_name(const char *name, u32 namesize)
{
    u32 i;

    if (namesize < 2 || name[namesize - 1] != '\0')
        return 0;

    for (i = 0; i < namesize - 1; i++) {
        if (name[i] == '\0' || name[i] == '\n' ||
            name[i] == '\r' || name[i] == '\t')
            return 0;
    }

    return (int)namesize <= RAMFS_NAME_MAX;
}

int valid_command(const char *name)
{
    unsigned int i;

    if (!name || !*name)
        return CMD_UNKNOWN;

    for (i = 0; i < sizeof(cmd_table) / sizeof(cmd_table[0]); i++) {
        if (klibc.strcmp(cmd_table[i].name, name) == 0)
            return (int)i + 1;
    }

    return CMD_UNKNOWN;
}

int valid_args(const char *name, int nargs)
{
    int id = valid_command(name) - 1;

    if (id < 0)
        return 0;

    return nargs >= cmd_table[id].min_args && nargs <= cmd_table[id].max_args;
}
