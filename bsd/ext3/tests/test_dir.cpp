#include "ext3/include/dir.hpp"
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

TEST(dir_entry_struct_layout)
{
    klog("bsd.ext3.test", "test_dir: dir entry struct");
    ASSERT(sizeof(ext3::dir_entry_disk) >= 8);
}

TEST(dir_entry_type_constants)
{
    klog("bsd.ext3.test", "test_dir: entry type constants");
    ASSERT(ext3::EXT3_FT_UNKNOWN == 0);
    ASSERT(ext3::EXT3_FT_REG_FILE == 1);
    ASSERT(ext3::EXT3_FT_DIR == 2);
    ASSERT(ext3::EXT3_FT_CHRDEV == 3);
    ASSERT(ext3::EXT3_FT_BLKDEV == 4);
    ASSERT(ext3::EXT3_FT_FIFO == 5);
    ASSERT(ext3::EXT3_FT_SOCK == 6);
    ASSERT(ext3::EXT3_FT_SYMLINK == 7);
}

TEST(dir_entry_max_name_len)
{
    klog("bsd.ext3.test", "test_dir: name length");
    ASSERT(ext3::EXT3_NAME_LEN == 255);
}

TEST(dir_default_state)
{
    klog("bsd.ext3.test", "test_dir: default state");
    ext3::Directory dir;
    ASSERT(dir.entry_count() == 0);
    ASSERT(dir.size() == 0);
}
