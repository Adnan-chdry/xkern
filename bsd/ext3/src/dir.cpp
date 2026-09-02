#include "ext3/include/dir.hpp"
#include "ext3/include/inode.hpp"
#include "ext3/include/superblock.hpp"
#include "ext3/disk.hpp"

extern "C" {
#include "klog.h"
#include "string.h"
#include "pmm.h"
}

namespace ext3 {

Directory::Directory()
    : m_data(nullptr),
      m_size(0),
      m_entry_count(0)
{
}

int Directory::read(class Disk &disk, const Superblock &sb, const Inode &inode)
{
    free();

    if (!inode.is_dir()) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "dir: inode is not a directory");
        return -1;
    }

    m_size = inode.size();
    if (m_size == 0)
        return 0;

    u32 alloc = (m_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    u64 addr = pmm_alloc();
    if (addr == 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "dir: out of memory (%u bytes)", m_size);
        return -1;
    }
    m_data = (u8 *)addr;
    memset(m_data, 0, alloc);

    int bytes = inode.read_data(disk, sb, 0, m_size, m_data);
    if (bytes <= 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "dir: failed to read directory data");
        pmm_free(addr);
        m_data = nullptr;
        return -1;
    }

    m_entry_count = 0;
    u32 pos = 0;
    while (pos < (u32)bytes) {
        dir_entry_disk *de = (dir_entry_disk *)(m_data + pos);
        if (de->inode == 0 && de->rec_len == 0)
            break;
        if (de->rec_len == 0)
            break;
        m_entry_count++;
        pos += de->rec_len;
    }

    klog("bsd.ext3", "dir: read %u entries (%u bytes)", m_entry_count, m_size);
    return 0;
}

void Directory::free()
{
    if (m_data) {
        pmm_free((u64)m_data);
        m_data = nullptr;
    }
    m_size = 0;
    m_entry_count = 0;
}

int Directory::lookup(const char *name, u32 *out_ino) const
{
    if (!m_data || !name)
        return -1;

    u32 name_len = strlen(name);
    u32 pos = 0;

    while (pos < m_size) {
        dir_entry_disk *de = (dir_entry_disk *)(m_data + pos);
        if (de->inode == 0) {
            if (de->rec_len == 0) break;
            pos += de->rec_len;
            continue;
        }

        if (de->name_len == name_len) {
            if (strncmp(de->name, name, name_len) == 0) {
                if (out_ino) *out_ino = de->inode;
                return 0;
            }
        }

        if (de->rec_len == 0) break;
        pos += de->rec_len;
    }

    return -1;
}

int Directory::get_entry(u32 index, DirEntry *out) const
{
    if (!m_data || !out)
        return -1;

    u32 pos = 0;
    u32 i = 0;

    while (pos < m_size) {
        dir_entry_disk *de = (dir_entry_disk *)(m_data + pos);
        if (de->inode == 0) {
            if (de->rec_len == 0) break;
            pos += de->rec_len;
            continue;
        }

        if (i == index) {
            out->inode = de->inode;
            out->rec_len = de->rec_len;
            out->name_len = de->name_len;
            out->file_type = de->file_type;
            u32 copy_len = de->name_len;
            if (copy_len > EXT3_NAME_LEN) copy_len = EXT3_NAME_LEN;
            memcpy(out->name, de->name, copy_len);
            out->name[copy_len] = '\0';
            return 0;
        }

        if (de->rec_len == 0) break;
        pos += de->rec_len;
        i++;
    }

    return -1;
}

void Directory::dump() const
{
    klog("bsd.ext3", "dir: %u entries, %u bytes", m_entry_count, m_size);

    u32 pos = 0;
    u32 i = 0;
    while (pos < m_size) {
        dir_entry_disk *de = (dir_entry_disk *)(m_data + pos);
        if (de->inode == 0) {
            if (de->rec_len == 0) break;
            pos += de->rec_len;
            continue;
        }

        char name_buf[256];
        u32 nlen = de->name_len;
        if (nlen > EXT3_NAME_LEN) nlen = EXT3_NAME_LEN;
        memcpy(name_buf, de->name, nlen);
        name_buf[nlen] = '\0';

        klog("bsd.ext3", "  [%u] ino=%u type=%u \"%s\"", i, de->inode, de->file_type, name_buf);

        if (de->rec_len == 0) break;
        pos += de->rec_len;
        i++;
    }
}

} // namespace ext3
