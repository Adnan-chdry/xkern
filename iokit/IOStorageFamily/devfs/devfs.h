#ifndef DEVFS_H
#define DEVFS_H

#include "types.h"

#define DEVFS_NAME_MAX    24
#define DEVFS_MAX_DEVICES 32

enum devfs_type {
    DEVFS_BLOCK_DEV,
    DEVFS_CHAR_DEV,
};

struct devfs_device;

typedef int (*devfs_rw_t)(struct devfs_device *dev, u32 lba, u8 count, void *buf);

struct devfs_device {
    char name[DEVFS_NAME_MAX];
    enum devfs_type type;
    u32 block_size;
    u32 block_count;
    char model[48];
    void *priv;
    devfs_rw_t read;
    devfs_rw_t write;
};

int devfs_init(void);
int devfs_register(struct devfs_device *dev);
int devfs_unregister(const char *name);
struct devfs_device *devfs_find(const char *name);
int devfs_count(void);
struct devfs_device *devfs_get(int i);
void devfs_list(void);
int devfs_read(const char *name, u32 lba, u8 count, void *buf);
int devfs_write(const char *name, u32 lba, u8 count, void *buf);

#endif
