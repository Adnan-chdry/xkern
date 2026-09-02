#pragma once

#include "types.h"

namespace ext3 {

enum {
    EXT3_MAGIC           = 0xEF53,
    EXT3_MIN_BLOCK_SIZE  = 1024,
    EXT3_MAX_BLOCK_SIZE  = 65536,
    EXT3_MIN_INODE_SIZE  = 128,
    EXT3_NAME_LEN        = 255,
    EXT3_N_BLOCKS        = 15,
    EXT3_DIRECT_BLOCKS   = 12,
    EXT3_ROOT_INO        = 2,
    EXT3_GOOD_OLD_FIRST_INO = 11,
};

enum ext3_super_state {
    EXT3_VALID_FS   = 0x0001,
    EXT3_ERROR_FS   = 0x0002,
    EXT3_ORPHAN_FS  = 0x0004,
};

enum ext3_super_errors {
    EXT3_ERRORS_CONTINUE = 1,
    EXT3_ERRORS_RO       = 2,
    EXT3_ERRORS_PANIC    = 3,
};

enum ext3_feature_compat {
    EXT3_FEATURE_COMPAT_DIR_PREALLOC  = 0x0001,
    EXT3_FEATURE_COMPAT_IMAGIC_INODES = 0x0002,
    EXT3_FEATURE_COMPAT_HAS_JOURNAL  = 0x0004,
    EXT3_FEATURE_COMPAT_EXT_ATTR     = 0x0008,
    EXT3_FEATURE_COMPAT_RESIZE_INODE = 0x0010,
    EXT3_FEATURE_COMPAT_DIR_INDEX    = 0x0020,
};

enum ext3_feature_incompat {
    EXT3_FEATURE_INCOMPAT_COMPRESSION  = 0x0001,
    EXT3_FEATURE_INCOMPAT_FILETYPE     = 0x0002,
    EXT3_FEATURE_INCOMPAT_RECOVER      = 0x0004,
    EXT3_FEATURE_INCOMPAT_JOURNAL_DEV  = 0x0008,
    EXT3_FEATURE_INCOMPAT_META_BG      = 0x0010,
};

enum ext3_feature_ro_compat {
    EXT3_FEATURE_RO_COMPAT_SPARSE_SUPER = 0x0001,
    EXT3_FEATURE_RO_COMPAT_LARGE_FILE   = 0x0002,
    EXT3_FEATURE_RO_COMPAT_BTREE_DIR    = 0x0004,
};

enum ext3_inode_mode {
    EXT3_S_IFIFO  = 0x1000,
    EXT3_S_IFCHR  = 0x2000,
    EXT3_S_IFDIR  = 0x4000,
    EXT3_S_IFBLK  = 0x6000,
    EXT3_S_IFREG  = 0x8000,
    EXT3_S_IFLNK  = 0xA000,
    EXT3_S_IFSOCK = 0xC000,
    EXT3_S_IFMT   = 0xF000,
};

enum ext3_inode_flags {
    EXT3_SYNC_FL        = 0x8000,
    EXT3_IMMUTABLE_FL   = 0x0001,
    EXT3_APPEND_FL      = 0x0004,
    EXT3_NODUMP_FL      = 0x0040,
    EXT3_NOATIME_FL     = 0x0080,
};

enum ext3_dir_entry_type {
    EXT3_FT_UNKNOWN  = 0,
    EXT3_FT_REG_FILE = 1,
    EXT3_FT_DIR      = 2,
    EXT3_FT_CHRDEV   = 3,
    EXT3_FT_BLKDEV   = 4,
    EXT3_FT_FIFO     = 5,
    EXT3_FT_SOCK     = 6,
    EXT3_FT_SYMLINK  = 7,
};

enum {
    EXT3_BLOCK_SIZE_1K  = 1024,
    EXT3_BLOCK_SIZE_2K  = 2048,
    EXT3_BLOCK_SIZE_4K  = 4096,
};

} // namespace ext3
