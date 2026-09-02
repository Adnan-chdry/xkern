#pragma once
#include "types.h"

void io_storage_init(void);

/* assign a node name by device type; partition < 0 = whole device */
int io_storage_assign(int type, int partition, char *out, u32 out_size);

enum {
    IO_STOR_LOOP,
    IO_STOR_DISK,
    IO_STOR_NVME,
    IO_STOR_SSD,
};