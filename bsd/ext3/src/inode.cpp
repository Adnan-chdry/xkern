#include "ext3/include/inode.hpp"
#include "ext3/include/superblock.hpp"
#include "ext3/include/blk_group.hpp"
#include "ext3/disk.hpp"

extern "C" {
#include "klog.h"
#include "string.h"
#include "pmm.h"
}

namespace ext3 {

Inode::Inode()
    : m_number(0)
{
    memset(&m_disk, 0, sizeof(m_disk));
}

int Inode::read(class Disk &disk, const Superblock &sb, u32 inode_no)
{
    (void)disk;
    (void)sb;
    if (inode_no == 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "inode: invalid inode number 0");
        return -1;
    }
    m_number = inode_no;
    return 0;
}

int Inode::read_from_disk(class Disk &disk, const Superblock &sb,
                           const BlockGroupDescriptorTable &bgt, u32 inode_no)
{
    if (inode_no == 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "inode: invalid inode number 0");
        return -1;
    }

    u32 inodes_per_group = sb.inodes_per_group();
    u32 inode_size = sb.inode_size();
    u32 group = (inode_no - 1) / inodes_per_group;
    u32 index = (inode_no - 1) % inodes_per_group;

    u32 inode_table_blk = bgt.inode_table(group);
    if (inode_table_blk == 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "inode: no inode table for group %u", group);
        return -1;
    }

    u32 offset_in_table = index * inode_size;
    u32 block_offset = offset_in_table % sb.block_size();
    u32 block_no = inode_table_blk + (offset_in_table / sb.block_size());

    u8 block_buf[EXT3_MAX_BLOCK_SIZE];
    if (disk.read_block(block_no, sb.block_size(), block_buf) != 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "inode: failed to read block %u", block_no);
        return -1;
    }

    memcpy(&m_disk, block_buf + block_offset, sizeof(m_disk));
    m_number = inode_no;

    return 0;
}

u32 Inode::block_addr(u32 logical) const
{
    if (logical < EXT3_DIRECT_BLOCKS)
        return m_disk.i_block[logical];
    return 0;
}

int Inode::read_data(class Disk &disk, const Superblock &sb,
                     u32 offset, u32 len, void *buf) const
{
    u32 block_size = sb.block_size();
    u8 *out = (u8 *)buf;
    u32 bytes_read = 0;

    while (bytes_read < len) {
        u32 file_off = offset + bytes_read;
        if (file_off >= m_disk.i_size)
            break;

        u32 logical_blk = file_off / block_size;
        u32 blk_offset = file_off % block_size;
        u32 to_copy = block_size - blk_offset;
        if (to_copy > len - bytes_read)
            to_copy = len - bytes_read;
        if (to_copy > m_disk.i_size - file_off)
            to_copy = m_disk.i_size - file_off;

        u32 phys_blk = 0;

        if (logical_blk < EXT3_DIRECT_BLOCKS) {
            phys_blk = m_disk.i_block[logical_blk];
        } else {
            u32 indirect_cap = block_size / 4;
            if (logical_blk < EXT3_DIRECT_BLOCKS + indirect_cap) {
                if (m_disk.i_block[EXT3_DIRECT_BLOCKS] == 0) {
                    memset(out + bytes_read, 0, to_copy);
                    bytes_read += to_copy;
                    continue;
                }
                u8 ind_buf[EXT3_MAX_BLOCK_SIZE];
                if (disk.read_block(m_disk.i_block[EXT3_DIRECT_BLOCKS],
                                    block_size, ind_buf) != 0)
                    return -1;
                u32 *ind = (u32 *)ind_buf;
                u32 idx = logical_blk - EXT3_DIRECT_BLOCKS;
                phys_blk = ind[idx];
            } else {
                memset(out + bytes_read, 0, to_copy);
                bytes_read += to_copy;
                continue;
            }
        }

        if (phys_blk == 0) {
            memset(out + bytes_read, 0, to_copy);
        } else {
            u8 tmp[EXT3_MAX_BLOCK_SIZE];
            if (disk.read_block(phys_blk, block_size, tmp) != 0)
                return -1;
            memcpy(out + bytes_read, tmp + blk_offset, to_copy);
        }
        bytes_read += to_copy;
    }
    return (s32)bytes_read;
}

u32 Inode::ind_block(class Disk &disk, const Superblock &sb, u32 blk) const
{
    (void)disk;
    (void)sb;
    return blk;
}

u32 Inode::dind_block(class Disk &disk, const Superblock &sb, u32 blk) const
{
    (void)disk;
    (void)sb;
    return blk;
}

u32 Inode::tind_block(class Disk &disk, const Superblock &sb, u32 blk) const
{
    (void)disk;
    (void)sb;
    return blk;
}

int Inode::read_indirect(class Disk &disk, const Superblock &sb,
                         u32 blk, u32 offset, u32 count, void *buf) const
{
    (void)disk;
    (void)sb;
    (void)blk;
    (void)offset;
    (void)count;
    (void)buf;
    return 0;
}

void Inode::dump() const
{
    klog("bsd.ext3", "inode[%u]: mode=0%o, size=%u, blocks=%u, links=%u",
         m_number, m_disk.i_mode, m_disk.i_size, m_disk.i_blocks, m_disk.i_links_count);
}

} // namespace ext3
