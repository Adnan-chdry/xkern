#include "ext3/include/blk_group.hpp"
#include "ext3/include/superblock.hpp"
#include "ext3/disk.hpp"

extern "C" {
#include "klog.h"
#include "string.h"
#include "pmm.h"
}

namespace ext3 {

static const u32 MAX_TABLE_GROUPS = 128;

BlockGroupDescriptorTable::BlockGroupDescriptorTable()
    : m_table(nullptr),
      m_count(0),
      m_block_size(0)
{
}

int BlockGroupDescriptorTable::read(class Disk &disk, const Superblock &sb)
{
    m_block_size = sb.block_size();
    m_count = sb.groups_count();

    if (m_count == 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "blk_group: no block groups");
        return -1;
    }

    if (m_count > MAX_TABLE_GROUPS)
        m_count = MAX_TABLE_GROUPS;

    u32 table_size = m_count * sizeof(blk_group_disk);
    u32 alloc_size = (table_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    u64 addr = pmm_alloc();
    if (addr == 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "blk_group: out of memory");
        return -1;
    }
    m_table = (blk_group_disk *)addr;
    memset(m_table, 0, alloc_size);

    u32 start_block = sb.first_data_block() + 1;
    u32 table_blocks = (table_size + m_block_size - 1) / m_block_size;

    if (disk.read_blocks(start_block, table_blocks, m_block_size, m_table) != 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "blk_group: failed to read descriptor table");
        pmm_free(addr);
        m_table = nullptr;
        return -1;
    }

    klog("bsd.ext3", "blk_group: read %u block group descriptors", m_count);
    return 0;
}

void BlockGroupDescriptorTable::free()
{
    if (m_table) {
        pmm_free((u64)m_table);
        m_table = nullptr;
    }
    m_count = 0;
}

const blk_group_disk *BlockGroupDescriptorTable::get(u32 index) const
{
    if (index >= m_count || !m_table)
        return nullptr;
    return &m_table[index];
}

u32 BlockGroupDescriptorTable::block_bitmap(u32 group) const
{
    const blk_group_disk *bg = get(group);
    return bg ? bg->bg_block_bitmap : 0;
}

u32 BlockGroupDescriptorTable::inode_bitmap(u32 group) const
{
    const blk_group_disk *bg = get(group);
    return bg ? bg->bg_inode_bitmap : 0;
}

u32 BlockGroupDescriptorTable::inode_table(u32 group) const
{
    const blk_group_disk *bg = get(group);
    return bg ? bg->bg_inode_table : 0;
}

u16 BlockGroupDescriptorTable::free_blocks(u32 group) const
{
    const blk_group_disk *bg = get(group);
    return bg ? bg->bg_free_blocks_count : 0;
}

u16 BlockGroupDescriptorTable::free_inodes(u32 group) const
{
    const blk_group_disk *bg = get(group);
    return bg ? bg->bg_free_inodes_count : 0;
}

u16 BlockGroupDescriptorTable::dirs_count(u32 group) const
{
    const blk_group_disk *bg = get(group);
    return bg ? bg->bg_dirs_count : 0;
}

void BlockGroupDescriptorTable::dump() const
{
    klog("bsd.ext3", "blk_group: %u groups", m_count);
    for (u32 i = 0; i < m_count; i++) {
        const blk_group_disk *bg = get(i);
        if (!bg) continue;
        klog("bsd.ext3", "  group[%u]: blk_bmp=%u, ino_bmp=%u, ino_tbl=%u, "
             "free_blk=%u, free_ino=%u, dirs=%u",
             i, bg->bg_block_bitmap, bg->bg_inode_bitmap, bg->bg_inode_table,
             bg->bg_free_blocks_count, bg->bg_free_inodes_count, bg->bg_dirs_count);
    }
}

} // namespace ext3
