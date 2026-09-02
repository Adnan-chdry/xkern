#pragma once

#include "types.h"
#include "ext3/include/const.hpp"

namespace ext3 {

#pragma pack(push, 1)
struct inode_disk {
    u16 i_mode;
    u16 i_uid;
    u32 i_size;
    u32 i_atime;
    u32 i_ctime;
    u32 i_mtime;
    u32 i_dtime;
    u16 i_gid;
    u16 i_links_count;
    u32 i_blocks;
    u32 i_flags;
    u32 i_block[EXT3_N_BLOCKS];
    u32 i_generation;
    u32 i_file_acl;
    u32 i_dir_acl;
    u32 i_faddr;
    u8  i_osd2[12];
};
#pragma pack(pop)

class Disk;
class Superblock;
class BlockGroupDescriptorTable;

class Inode {
public:
    Inode();

    int  read(class Disk &disk, const Superblock &sb, u32 inode_no);
    int  read_from_disk(class Disk &disk, const Superblock &sb,
                         const BlockGroupDescriptorTable &bgt, u32 inode_no);
    void dump() const;

    u32  number() const { return m_number; }
    u16  mode() const { return m_disk.i_mode; }
    u32  size() const { return m_disk.i_size; }
    u32  blocks() const { return m_disk.i_blocks; }
    u16  links() const { return m_disk.i_links_count; }
    u16  uid() const { return m_disk.i_uid; }
    u16  gid() const { return m_disk.i_gid; }
    u32  atime() const { return m_disk.i_atime; }
    u32  ctime() const { return m_disk.i_ctime; }
    u32  mtime() const { return m_disk.i_mtime; }
    u32  flags() const { return m_disk.i_flags; }
    u32  dir_acl() const { return m_disk.i_dir_acl; }

    bool is_dir() const { return (m_disk.i_mode & EXT3_S_IFMT) == EXT3_S_IFDIR; }
    bool is_reg() const { return (m_disk.i_mode & EXT3_S_IFMT) == EXT3_S_IFREG; }
    bool is_link() const { return (m_disk.i_mode & EXT3_S_IFMT) == EXT3_S_IFLNK; }

    u32  block_addr(u32 logical) const;

    const inode_disk &raw() const { return m_disk; }
    const u32 *block_list() const { return m_disk.i_block; }

    int  read_data(class Disk &disk, const Superblock &sb,
                   u32 offset, u32 len, void *buf) const;

private:
    u32  ind_block(class Disk &disk, const Superblock &sb, u32 blk) const;
    u32  dind_block(class Disk &disk, const Superblock &sb, u32 blk) const;
    u32  tind_block(class Disk &disk, const Superblock &sb, u32 blk) const;
    int  read_indirect(class Disk &disk, const Superblock &sb,
                       u32 blk, u32 offset, u32 count, void *buf) const;

    inode_disk m_disk;
    u32        m_number;
};

} // namespace ext3
