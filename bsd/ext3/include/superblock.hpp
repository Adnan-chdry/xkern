#pragma once

#include "types.h"
#include "ext3/include/const.hpp"

namespace ext3 {

#pragma pack(push, 1)
struct superblock_disk {
    u32 s_inodes_count;
    u32 s_blocks_count;
    u32 s_r_blocks_count;
    u32 s_free_blocks_count;
    u32 s_free_inodes_count;
    u32 s_first_data_block;
    u32 s_log_block_size;
    u32 s_log_frag_size;
    u32 s_blocks_per_group;
    u32 s_frags_per_group;
    u32 s_inodes_per_group;
    u32 s_mtime;
    u32 s_wtime;
    u16 s_mnt_count;
    u16 s_max_mnt_count;
    u16 s_magic;
    u16 s_state;
    u16 s_errors;
    u16 s_minor_rev_level;
    u32 s_lastcheck;
    u32 s_checkinterval;
    u32 s_creator_os;
    u32 s_rev_level;
    u16 s_def_resuid;
    u16 s_def_resgid;
    u32 s_first_ino;
    u16 s_inode_size;
    u16 s_block_group_nr;
    u32 s_feature_compat;
    u32 s_feature_incompat;
    u32 s_feature_ro_compat;
    u8  s_uuid[16];
    char s_volume_name[16];
    char s_last_mounted[64];
    u32 s_algorithm_usage_bitmap;
    u8  s_prealloc_blocks;
    u8  s_prealloc_dir_blocks;
    u16 s_padding1;
    u8  s_journal_uuid[16];
    u32 s_journal_inum;
    u32 s_journal_dev;
    u32 s_last_orphan;
    u32 s_hash_seed[4];
    u8  s_def_hash_version;
    u8  s_padding2[3];
    u32 s_default_mount_options;
    u32 s_first_meta_bg;
};
#pragma pack(pop)

class Superblock {
public:
    Superblock();

    int  read(class Disk &disk);
    bool valid() const;
    void dump() const;

    u32  block_size() const;
    u32  inode_size() const;
    u32  inodes_per_group() const;
    u32  blocks_per_group() const;
    u32  first_data_block() const;
    u32  first_ino() const;
    u32  groups_count() const;
    u32  blocks_count() const;
    u32  inodes_count() const;

    const superblock_disk &raw() const { return m_sb; }

private:
    superblock_disk m_sb;
    u32             m_block_size;
    bool            m_valid;
};

} // namespace ext3
