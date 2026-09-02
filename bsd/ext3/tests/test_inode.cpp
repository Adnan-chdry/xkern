#include "ext3/include/inode.hpp"
#include "ext3/include/const.hpp"

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

TEST(inode_struct_layout)
{
    klog("bsd.ext3.test", "test_inode: struct layout");
    ASSERT(sizeof(ext3::inode_disk) == 128);
}

TEST(inode_default_state)
{
    klog("bsd.ext3.test", "test_inode: default state");
    ext3::Inode inode;
    ASSERT(inode.number() == 0);
    ASSERT(inode.size() == 0);
    ASSERT(inode.links() == 0);
    ASSERT(inode.mode() == 0);
    ASSERT(!inode.is_dir());
    ASSERT(!inode.is_reg());
    ASSERT(!inode.is_link());
}

TEST(inode_mode_checks)
{
    klog("bsd.ext3.test", "test_inode: mode checks via raw manipulation");
    ext3::inode_disk disk;
    memset(&disk, 0, sizeof(disk));

    disk.i_mode = ext3::EXT3_S_IFDIR | 0755;
    ASSERT((disk.i_mode & ext3::EXT3_S_IFMT) == ext3::EXT3_S_IFDIR);

    disk.i_mode = ext3::EXT3_S_IFREG | 0644;
    ASSERT((disk.i_mode & ext3::EXT3_S_IFMT) == ext3::EXT3_S_IFREG);

    disk.i_mode = ext3::EXT3_S_IFLNK | 0777;
    ASSERT((disk.i_mode & ext3::EXT3_S_IFMT) == ext3::EXT3_S_IFLNK);
}

TEST(inode_block_constants)
{
    klog("bsd.ext3.test", "test_inode: block constants");
    ASSERT(ext3::EXT3_DIRECT_BLOCKS == 12);
    ASSERT(ext3::EXT3_N_BLOCKS == 15);
}
