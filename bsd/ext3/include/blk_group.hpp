#pragma once

#include "types.h"

namespace ext3 {

#pragma pack(push, 1)
struct blk_group_disk {
    u32 bg_block_bitmap;
    u32 bg_inode_bitmap;
    u32 bg_inode_table;
    u16 bg_free_blocks_count;
    u16 bg_free_inodes_count;
    u16 bg_dirs_count;
    u16 bg_pad;
    u8  bg_reserved[12];
};
#pragma pack(pop)

class Disk;
class Superblock;

class BlockGroupDescriptorTable {
public:
    BlockGroupDescriptorTable();

    int  read(class Disk &disk, const Superblock &sb);
    void free();
    void dump() const;

    const blk_group_disk *get(u32 index) const;
    u32 count() const { return m_count; }

    u32 block_bitmap(u32 group) const;
    u32 inode_bitmap(u32 group) const;
    u32 inode_table(u32 group) const;
    u16 free_blocks(u32 group) const;
    u16 free_inodes(u32 group) const;
    u16 dirs_count(u32 group) const;

private:
    blk_group_disk *m_table;
    u32             m_count;
    u32             m_block_size;
};

} // namespace ext3
