#include "../ata.h"
#include "../devfs/devfs.h"
#include "io.h"
#include "klog.h"
#include "stdio.h"
#include "string.h"
#include "klibc.h"

#define ATA_MAX_DRIVES 4

static struct ata_device g_ata_devs[ATA_MAX_DRIVES];
static int g_ata_count;

static int ata_poll(u16 io) {
    for (int i = 0; i < 100000; i++) {
        u8 sts = inb(io + ATA_REG_COMMAND);
        if (!(sts & ATA_SR_BSY) && (sts & ATA_SR_RDY))
            return 0;
    }
    return -1;
}

static void ata_400ns_delay(u16 ctrl) {
    inb(ctrl);
    inb(ctrl);
    inb(ctrl);
    inb(ctrl);
}

int ata_identify(struct ata_device *dev) {
    u16 io = (dev->bus == 0) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
    u16 ctrl = (dev->bus == 0) ? ATA_PRIMARY_CTRL : ATA_SECONDARY_CTRL;
    u8 drive = (dev->drive == ATA_MASTER) ? 0xA0 : 0xB0;

    outb(ctrl, 0x04);
    outb(io + ATA_REG_DRIVE, drive);
    if (ata_poll(io) != 0) { dev->present = 0; return -1; }

    outb(io + ATA_REG_SECTORS, 0);
    outb(io + ATA_REG_LBA_LO, 0);
    outb(io + ATA_REG_LBA_MID, 0);
    outb(io + ATA_REG_LBA_HI, 0);
    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    u8 sts = inb(io + ATA_REG_COMMAND);
    if (sts == 0) {
        dev->present = 0;
        return -1;
    }

    if (ata_poll(io) != 0) { dev->present = 0; return -1; }

    u16 buf[256];
    u16 *data = (u16 *)buf;
    for (int i = 0; i < 256; i++)
        data[i] = inw(io + ATA_REG_DATA);

    dev->signature = buf[0];
    dev->capabilities = buf[49];
    dev->lba_sectors = *(u32 *)&buf[60];

    for (int i = 0; i < 40; i += 2) {
        dev->model[i] = (buf[27 + i / 2] >> 8) & 0xFF;
        dev->model[i + 1] = buf[27 + i / 2] & 0xFF;
    }
    dev->model[40] = 0;

    dev->present = 1;
    return 0;
}

int ata_read_sectors(struct ata_device *dev, u32 lba, u8 count, void *buf) {
    u16 io = (dev->bus == 0) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
    u16 ctrl = (dev->bus == 0) ? ATA_PRIMARY_CTRL : ATA_SECONDARY_CTRL;
    u8 drive = (dev->drive == ATA_MASTER) ? 0xE0 : 0xF0;

    outb(io + ATA_REG_FEATURES, 0);
    outb(io + ATA_REG_SECTORS, count);
    outb(io + ATA_REG_LBA_LO, lba & 0xFF);
    outb(io + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(io + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(io + ATA_REG_DRIVE, drive | ((lba >> 24) & 0x0F));
    outb(io + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    u16 *ptr = (u16 *)buf;
    for (int sect = 0; sect < count; sect++) {
        if (ata_poll(io) != 0) return -1;
        u8 sts = inb(io + ATA_REG_COMMAND);
        if (sts & ATA_SR_ERR)
            return -1;
        for (int i = 0; i < 256; i++)
            ptr[sect * 256 + i] = inw(io + ATA_REG_DATA);
        ata_400ns_delay(ctrl);
    }

    return 0;
}

int ata_write_sectors(struct ata_device *dev, u32 lba, u8 count, void *buf) {
    u16 io = (dev->bus == 0) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
    u16 ctrl = (dev->bus == 0) ? ATA_PRIMARY_CTRL : ATA_SECONDARY_CTRL;
    u8 drive = (dev->drive == ATA_MASTER) ? 0xE0 : 0xF0;

    outb(io + ATA_REG_FEATURES, 0);
    outb(io + ATA_REG_SECTORS, count);
    outb(io + ATA_REG_LBA_LO, lba & 0xFF);
    outb(io + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(io + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(io + ATA_REG_DRIVE, drive | ((lba >> 24) & 0x0F));
    outb(io + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    u16 *ptr = (u16 *)buf;
    for (int sect = 0; sect < count; sect++) {
        if (ata_poll(io) != 0) return -1;
        u8 sts = inb(io + ATA_REG_COMMAND);
        if (sts & ATA_SR_ERR)
            return -1;
        for (int i = 0; i < 256; i++)
            outw(io + ATA_REG_DATA, ptr[sect * 256 + i]);
        outb(io + ATA_REG_COMMAND, 0xE7);
        ata_400ns_delay(ctrl);
    }

    return 0;
}

static int ata_read_dev(struct devfs_device *ddev, u32 lba, u8 count, void *buf) {
    struct ata_device *dev = (struct ata_device *)ddev->priv;
    return ata_read_sectors(dev, lba, count, buf);
}

static int ata_write_dev(struct devfs_device *ddev, u32 lba, u8 count, void *buf) {
    struct ata_device *dev = (struct ata_device *)ddev->priv;
    return ata_write_sectors(dev, lba, count, buf);
}

int ata_init(void) {
    g_ata_count = 0;

    for (int bus = 0; bus < 2; bus++) {
        for (int drv = 0; drv < 2; drv++) {
            struct ata_device *dev = &g_ata_devs[g_ata_count];
            dev->bus = bus;
            dev->drive = drv;
            if (ata_identify(dev) == 0 && dev->present) {
                char name[DEVFS_NAME_MAX];
                char model[48];
                for (int i = 0; i < 40; i++)
                    model[i] = dev->model[i] ? dev->model[i] : ' ';
                model[40] = 0;
                klibc.snprintf(name, sizeof(name), "sd%c", 'a' + g_ata_count);

                struct devfs_device ddev;
                klibc.strncpy(ddev.name, name, DEVFS_NAME_MAX - 1);
                ddev.type = DEVFS_BLOCK_DEV;
                ddev.block_size = 512;
                ddev.block_count = dev->lba_sectors / 2048;
                klibc.strncpy(ddev.model, model, 47);
                ddev.model[47] = 0;
                ddev.priv = dev;
                ddev.read = ata_read_dev;
                ddev.write = ata_write_dev;

                if (devfs_register(&ddev) >= 0)
                    klog("ata", "%s -> /dev/%s  (%u mb)  %s",
                         name, name, dev->lba_sectors / 2048, model);
                g_ata_count++;
            }
        }
    }

    klog("ata", "ata_init() done");
    return 0;
}
