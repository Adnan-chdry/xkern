#include "ext3/disk.hpp"
#include "ext3/include/const.hpp"

extern "C" {
#include "klog.h"
#include "string.h"
}

namespace ext3 {

Disk::Disk()
    : m_dev(nullptr),
      m_sector_size(512),
      m_total_sectors(0),
      m_open(false)
{
    m_name[0] = '\0';
}

int Disk::open(const char *dev_name)
{
    m_dev = devfs_find(dev_name);
    if (!m_dev) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "disk: device '%s' not found", dev_name);
        return -1;
    }

    strncpy(m_name, dev_name, sizeof(m_name) - 1);
    m_name[sizeof(m_name) - 1] = '\0';
    m_sector_size = m_dev->block_size;
    m_total_sectors = m_dev->block_count;
    m_open = true;

    klog("bsd.ext3", "disk: opened '%s' (%u sectors, %u bytes/sector)",
         m_name, m_total_sectors, m_sector_size);
    return 0;
}

void Disk::close()
{
    m_dev = nullptr;
    m_open = false;
    m_name[0] = '\0';
}

int Disk::read_sectors(u32 lba, u32 count, void *buf)
{
    if (!m_open || !m_dev)
        return -1;

    if (lba + count > m_total_sectors) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "disk: read beyond end (lba=%u, cnt=%u, total=%u)",
                 lba, count, m_total_sectors);
        return -1;
    }

    u8 max_sectors = 128;
    u8 *ptr = (u8 *)buf;
    u32 remaining = count;

    while (remaining > 0) {
        u8 batch = (remaining > max_sectors) ? max_sectors : (u8)remaining;
        if (m_dev->read(m_dev, lba, batch, ptr) != 0) {
            klog_lvl(KLOG_ERR, "bsd.ext3", "disk: read failed at lba=%u", lba);
            return -1;
        }
        lba += batch;
        ptr += batch * m_sector_size;
        remaining -= batch;
    }
    return 0;
}

int Disk::write_sectors(u32 lba, u32 count, const void *buf)
{
    if (!m_open || !m_dev)
        return -1;

    if (lba + count > m_total_sectors)
        return -1;

    u8 max_sectors = 128;
    const u8 *ptr = (const u8 *)buf;
    u32 remaining = count;

    while (remaining > 0) {
        u8 batch = (remaining > max_sectors) ? max_sectors : (u8)remaining;
        if (m_dev->write(m_dev, lba, batch, (void *)ptr) != 0) {
            klog_lvl(KLOG_ERR, "bsd.ext3", "disk: write failed at lba=%u", lba);
            return -1;
        }
        lba += batch;
        ptr += batch * m_sector_size;
        remaining -= batch;
    }
    return 0;
}

int Disk::read_block(u32 block_no, u32 block_size, void *buf)
{
    u32 sectors_per_block = block_size / m_sector_size;
    u32 lba = block_no * sectors_per_block;
    return read_sectors(lba, sectors_per_block, buf);
}

int Disk::read_blocks(u32 block_no, u32 count, u32 block_size, void *buf)
{
    u32 sectors_per_block = block_size / m_sector_size;
    u32 lba = block_no * sectors_per_block;
    return read_sectors(lba, count * sectors_per_block, buf);
}

} // namespace ext3
