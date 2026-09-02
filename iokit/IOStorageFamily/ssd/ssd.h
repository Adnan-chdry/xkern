#ifndef SSD_H
#define SSD_H

#include "types.h"

int ssd_init(void);
int ssd_register(const char *model, u32 lba_count,
                 int (*read)(u32 lba, u8 count, void *buf),
                 int (*write)(u32 lba, u8 count, void *buf));
int ssd_read(int dev_id, u32 lba, u8 count, void *buf);
int ssd_write(int dev_id, u32 lba, u8 count, void *buf);

#endif
