#ifndef RAMFS_COMPAT_H
#define RAMFS_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

#define RAMFS_MAX_FILES 32
#define RAMFS_NAME_MAX  64

struct ramfs_file {
    char name[RAMFS_NAME_MAX];
    u8  *data;
    u32  size;
};

struct ramfs {
    struct ramfs_file files[RAMFS_MAX_FILES];
    int count;
};

void  ramfs_add(struct ramfs *fs, const char *name, u8 *data, u32 size);
struct ramfs_file *ramfs_lookup(struct ramfs *fs, const char *path);
struct ramfs *ramfs_get(void);
void  ramfs_list(struct ramfs *fs);
int   cpio_unpack(u8 *arc, u32 len, struct ramfs *fs);
int   valid_cpio_magic(const char *h);

#ifdef __cplusplus
}
#endif

#endif
