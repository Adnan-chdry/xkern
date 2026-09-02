#ifndef ATA_H
#define ATA_H

#include "types.h"

#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CTRL  0x376

#define ATA_MASTER  0x00
#define ATA_SLAVE   0x01

#define ATA_REG_DATA      0
#define ATA_REG_FEATURES  1
#define ATA_REG_SECTORS   2
#define ATA_REG_LBA_LO    3
#define ATA_REG_LBA_MID   4
#define ATA_REG_LBA_HI    5
#define ATA_REG_DRIVE     6
#define ATA_REG_COMMAND   7

#define ATA_CMD_READ_PIO      0x20
#define ATA_CMD_WRITE_PIO     0x30
#define ATA_CMD_IDENTIFY      0xEC

#define ATA_SR_ERR   0x01
#define ATA_SR_DRQ   0x08
#define ATA_SR_DF    0x20
#define ATA_SR_RDY   0x40
#define ATA_SR_BSY   0x80

struct ata_device {
    u8  bus;
    u8  drive;
    u16 signature;
    u16 capabilities;
    u32 lba_sectors;
    char model[41];
    int present;
};

int  ata_init(void);
int  ata_identify(struct ata_device *dev);
int  ata_read_sectors(struct ata_device *dev, u32 lba, u8 count, void *buf);
int  ata_write_sectors(struct ata_device *dev, u32 lba, u8 count, void *buf);

#endif
