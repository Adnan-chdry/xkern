#pragma once

#include "types.h"

extern "C" {
#include "klog.h"
#include "string.h"
#include "devfs/devfs.h"
}

namespace ext3 {

class Disk {
public:
    Disk();

    int  open(const char *dev_name);
    void close();

    int  read_sectors(u32 lba, u32 count, void *buf);
    int  write_sectors(u32 lba, u32 count, const void *buf);
    int  read_block(u32 block_no, u32 block_size, void *buf);
    int  read_blocks(u32 block_no, u32 count, u32 block_size, void *buf);

    u32  sector_size() const { return m_sector_size; }
    u32  total_sectors() const { return m_total_sectors; }
    bool is_open() const { return m_open; }
    const char *name() const { return m_name; }

private:
    ::devfs_device *m_dev;
    char                 m_name[24];
    u32                  m_sector_size;
    u32                  m_total_sectors;
    bool                 m_open;
};

} // namespace ext3
