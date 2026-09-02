#include "ext3/include/superblock.hpp"
#include "ext3/disk.hpp"

extern "C" {
#include "klog.h"
#include "string.h"
}

static int test_count = 0;
static int test_pass = 0;
static int test_fail = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct test_reg { \
        test_reg() { test_##name(); } \
    } reg_##name; \
    static void test_##name()

#define ASSERT(cond) do { \
    test_count++; \
    if (cond) { test_pass++; klog("bsd.ext3.test", "  PASS: %s", #cond); } \
    else { test_fail++; klog_lvl(KLOG_ERR, "bsd.ext3.test", "  FAIL: %s", #cond); } \
} while(0)

TEST(superblock_struct_size)
{
    klog("bsd.ext3.test", "test_superblock: struct size");
    ASSERT(sizeof(ext3::superblock_disk) == 1024);
}

TEST(superblock_constants)
{
    klog("bsd.ext3.test", "test_superblock: constants");
    ASSERT(ext3::EXT3_MAGIC == 0xEF53);
    ASSERT(ext3::EXT3_ROOT_INO == 2);
    ASSERT(ext3::EXT3_DIRECT_BLOCKS == 12);
    ASSERT(ext3::EXT3_N_BLOCKS == 15);
    ASSERT(ext3::EXT3_MIN_BLOCK_SIZE == 1024);
    ASSERT(ext3::EXT3_MIN_INODE_SIZE == 128);
}

TEST(superblock_default_state)
{
    klog("bsd.ext3.test", "test_superblock: default state");
    ext3::Superblock sb;
    ASSERT(!sb.valid());
    ASSERT(sb.block_size() == 0);
}

TEST(superblock_modes)
{
    klog("bsd.ext3.test", "test_superblock: inode mode flags");
    ASSERT(ext3::EXT3_S_IFDIR == 0x4000);
    ASSERT(ext3::EXT3_S_IFREG == 0x8000);
    ASSERT(ext3::EXT3_S_IFLNK == 0xA000);
    ASSERT(ext3::EXT3_S_IFCHR == 0x2000);
    ASSERT(ext3::EXT3_S_IFBLK == 0x6000);
}

TEST(superblock_dir_entry_types)
{
    klog("bsd.ext3.test", "test_superblock: dir entry types");
    ASSERT(ext3::EXT3_FT_UNKNOWN == 0);
    ASSERT(ext3::EXT3_FT_REG_FILE == 1);
    ASSERT(ext3::EXT3_FT_DIR == 2);
}
