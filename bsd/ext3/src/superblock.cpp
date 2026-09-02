#include "ext3/include/superblock.hpp"
#include "ext3/disk.hpp"

extern "C" {
#include "klog.h"
#include "string.h"
#include "stdio.h"
}

namespace ext3 {

Superblock::Superblock()
    : m_block_size(0),
      m_valid(false)
{
    memset(&m_sb, 0, sizeof(m_sb));
}

int Superblock::read(class Disk &disk)
{
    u8 buf[1024];
    if (disk.read_sectors(2, 2, buf) != 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "superblock: failed to read from disk");
        return -1;
    }

    memcpy(&m_sb, buf, sizeof(m_sb));

    if (m_sb.s_magic != EXT3_MAGIC) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "superblock: bad magic 0x%x (expected 0x%x)",
                 m_sb.s_magic, EXT3_MAGIC);
        m_valid = false;
        return -1;
    }

    m_block_size = EXT3_MIN_BLOCK_SIZE << m_sb.s_log_block_size;
    m_valid = true;

    klog("bsd.ext3", "superblock: valid ext3 fs, block_size=%u, blocks=%u, inodes=%u",
         m_block_size, m_sb.s_blocks_count, m_sb.s_inodes_count);
    return 0;
}

bool Superblock::valid() const
{
    return m_valid;
}

u32 Superblock::block_size() const
{
    return m_block_size;
}

u32 Superblock::inode_size() const
{
    if (m_sb.s_rev_level >= 1 && (m_sb.s_feature_incompat & 0x80))
        return m_sb.s_inode_size;
    return EXT3_MIN_INODE_SIZE;
}

u32 Superblock::inodes_per_group() const
{
    return m_sb.s_inodes_per_group;
}

u32 Superblock::blocks_per_group() const
{
    return m_sb.s_blocks_per_group;
}

u32 Superblock::first_data_block() const
{
    return m_sb.s_first_data_block;
}

u32 Superblock::first_ino() const
{
    if (m_sb.s_rev_level >= 1)
        return m_sb.s_first_ino;
    return EXT3_GOOD_OLD_FIRST_INO;
}

u32 Superblock::groups_count() const
{
    u32 blocks_per = m_sb.s_blocks_per_group;
    if (blocks_per == 0) return 0;
    u32 total = m_sb.s_blocks_count - m_sb.s_first_data_block;
    return (total + blocks_per - 1) / blocks_per;
}

u32 Superblock::blocks_count() const
{
    return m_sb.s_blocks_count;
}

u32 Superblock::inodes_count() const
{
    return m_sb.s_inodes_count;
}

void Superblock::dump() const
{
    if (!m_valid) {
        klog("bsd.ext3", "superblock: not valid");
        return;
    }

    char uuid_str[40];
    const u8 *u = m_sb.s_uuid;
    snprintf(uuid_str, sizeof(uuid_str),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
             u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);

    klog("bsd.ext3", "  volume:    %s", m_sb.s_volume_name);
    klog("bsd.ext3", "  uuid:      %s", uuid_str);
    klog("bsd.ext3", "  blocks:    %u (first_data=%u)", m_sb.s_blocks_count, m_sb.s_first_data_block);
    klog("bsd.ext3", "  inodes:    %u", m_sb.s_inodes_count);
    klog("bsd.ext3", "  block_size:%u", m_block_size);
    klog("bsd.ext3", "  inode_size:%u", inode_size());
    klog("bsd.ext3", "  groups:    %u", groups_count());
    klog("bsd.ext3", "  state:     %s", (m_sb.s_state & EXT3_VALID_FS) ? "clean" : "error");
    klog("bsd.ext3", "  mounted:   %u times", m_sb.s_mnt_count);
}

} // namespace ext3
