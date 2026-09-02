#include "devfs.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include "klibc.h"

static struct devfs_device g_devices[DEVFS_MAX_DEVICES];
static int g_dev_count;

int __devfs_init(void) {
    g_dev_count = 0;
    klog("devfs", "devfs_init() done");
    return 0;
}

int devfs_register(struct devfs_device *dev) {
    if (g_dev_count >= DEVFS_MAX_DEVICES)
        return -1;
    if (devfs_find(dev->name))
        return -1;
    struct devfs_device *slot = &g_devices[g_dev_count];
    klibc.strncpy(slot->name, dev->name, DEVFS_NAME_MAX - 1);
    slot->name[DEVFS_NAME_MAX - 1] = 0;
    slot->type = dev->type;
    slot->block_size = dev->block_size;
    slot->block_count = dev->block_count;
    klibc.strncpy(slot->model, dev->model, 47);
    slot->model[47] = 0;
    slot->priv = dev->priv;
    slot->read = dev->read;
    slot->write = dev->write;
    g_dev_count++;
    return g_dev_count - 1;
}

int devfs_unregister(const char *name) {
    for (int i = 0; i < g_dev_count; i++) {
        if (klibc.strcmp(g_devices[i].name, name) == 0) {
            for (int j = i; j < g_dev_count - 1; j++)
                g_devices[j] = g_devices[j + 1];
            g_dev_count--;
            return 0;
        }
    }
    return -1;
}

struct devfs_device *devfs_find(const char *name) {
    for (int i = 0; i < g_dev_count; i++) {
        if (klibc.strcmp(g_devices[i].name, name) == 0)
            return &g_devices[i];
    }
    return 0;
}

int devfs_count(void) {
    return g_dev_count;
}

struct devfs_device *devfs_get(int i) {
    if (i < 0 || i >= g_dev_count)
        return 0;
    return &g_devices[i];
}

void devfs_list(void) {
    klog("devfs", "registered devices:");
    for (int i = 0; i < g_dev_count; i++) {
        struct devfs_device *d = &g_devices[i];
        klog("devfs", "  /dev/%s (%s, %u mb, %s",
             d->name,
             d->type == DEVFS_BLOCK_DEV ? "blk" : "chr",
             (u32)d->block_count,
             d->model);
    }
}

int devfs_read(const char *name, u32 lba, u8 count, void *buf) {
    struct devfs_device *d = devfs_find(name);
    if (!d || !d->read)
        return -1;
    return d->read(d, lba, count, buf);
}

int devfs_write(const char *name, u32 lba, u8 count, void *buf) {
    struct devfs_device *d = devfs_find(name);
    if (!d || !d->write)
        return -1;
    return d->write(d, lba, count, buf);
}
