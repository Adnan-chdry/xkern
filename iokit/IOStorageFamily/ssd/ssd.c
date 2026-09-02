#include "klog.h"
#include "types.h"
#include "string.h"
#include "stdio.h"
#include "devfs/devfs.h"
#include "klibc.h"

#define SSD_MAX_DEVICES 8
#define SSD_SECTOR_SIZE 512

struct ssd_device {
    int   present;
    u32   lba_count;
    u32   sector_size;
    char  model[41];
    int   (*read)(u32 lba, u8 count, void *buf);
    int   (*write)(u32 lba, u8 count, void *buf);
};

static struct ssd_device g_ssd_devs[SSD_MAX_DEVICES];
static int g_ssd_count;

int ssd_register(const char *model, u32 lba_count,
                 int (*read)(u32 lba, u8 count, void *buf),
                 int (*write)(u32 lba, u8 count, void *buf)) {
    if (g_ssd_count >= SSD_MAX_DEVICES)
        return -1;

    struct ssd_device *dev = &g_ssd_devs[g_ssd_count];
    dev->present = 1;
    dev->lba_count = lba_count;
    dev->sector_size = SSD_SECTOR_SIZE;
    dev->read = read;
    dev->write = write;
    klibc.strncpy(dev->model, model, 40);
    dev->model[40] = 0;

    char name[DEVFS_NAME_MAX];
    klibc.snprintf(name, sizeof(name), "ssd%c", 'a' + g_ssd_count);

    struct devfs_device ddev;
    klibc.snprintf(ddev.name, sizeof(ddev.name), "%s", name);
    ddev.type = DEVFS_BLOCK_DEV;
    ddev.block_size = SSD_SECTOR_SIZE;
    ddev.block_count = lba_count / 2048;
    klibc.snprintf(ddev.model, sizeof(ddev.model), "%s", model);
    ddev.priv = 0;
    ddev.read = 0;
    ddev.write = 0;
    devfs_register(&ddev);

    klog("SSD", "%s -> /dev/%s  (%u mb)", name, name, lba_count / 2048);

    g_ssd_count++;
    return g_ssd_count - 1;
}

int ssd_read(int dev_id, u32 lba, u8 count, void *buf) {
    if (dev_id < 0 || dev_id >= g_ssd_count)
        return -1;
    struct ssd_device *dev = &g_ssd_devs[dev_id];
    if (!dev->present || !dev->read)
        return -1;
    return dev->read(lba, count, buf);
}

int ssd_write(int dev_id, u32 lba, u8 count, void *buf) {
    if (dev_id < 0 || dev_id >= g_ssd_count)
        return -1;
    struct ssd_device *dev = &g_ssd_devs[dev_id];
    if (!dev->present || !dev->write)
        return -1;
    return dev->write(lba, count, buf);
}

int ssd_init(void) {
    g_ssd_count = 0;
    klog("ssd", "ssd_init() done");
    return 0;
}
