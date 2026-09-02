#include "ext3/include/fs.hpp"
#include "ext3/disk.hpp"

extern "C" {
#include "klog.h"
#include "string.h"
#include "stdio.h"
#include "pmm.h"
}

namespace ext3 {

static FileSystem *g_fs_instance = nullptr;

static const char *skip_slash(const char *p)
{
    while (*p == '/') p++;
    return p;
}

static const char *find_next_slash(const char *p)
{
    while (*p && *p != '/') p++;
    return (*p == '/') ? p : nullptr;
}

FileSystem::FileSystem()
    : m_disk(nullptr),
      m_mounted(false)
{
    m_dev[0] = '\0';
    for (u32 i = 0; i < MAX_OPEN_FILES; i++)
        m_files[i].in_use = false;
}

int FileSystem::mount(Disk *disk, const char *dev_name)
{
    if (m_mounted) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "fs: already mounted");
        return -1;
    }

    if (disk->open(dev_name) != 0)
        return -1;

    m_disk = disk;

    if (m_sb.read(*m_disk) != 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "fs: failed to read superblock");
        m_disk->close();
        m_disk = nullptr;
        return -1;
    }

    m_sb.dump();

    if (m_bgt.read(*m_disk, m_sb) != 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "fs: failed to read block group descriptors");
        m_disk->close();
        m_disk = nullptr;
        return -1;
    }

    m_bgt.dump();

    strncpy(m_dev, dev_name, sizeof(m_dev) - 1);
    m_dev[sizeof(m_dev) - 1] = '\0';
    m_mounted = true;
    g_fs_instance = this;

    klog("bsd.ext3", "fs: mounted ext3 on '%s'", m_dev);
    return 0;
}

int FileSystem::unmount()
{
    if (!m_mounted) return -1;

    for (u32 i = 0; i < MAX_OPEN_FILES; i++) {
        if (m_files[i].in_use)
            m_files[i].in_use = false;
    }

    m_bgt.free();
    m_disk->close();
    m_disk = nullptr;
    m_mounted = false;
    g_fs_instance = nullptr;

    klog("bsd.ext3", "fs: unmounted");
    return 0;
}

int FileSystem::resolve_path(const char *path, u32 *out_ino)
{
    if (!path || path[0] != '/') return -1;

    u32 current_ino = EXT3_ROOT_INO;
    const char *p = skip_slash(path);

    if (*p == '\0') {
        *out_ino = current_ino;
        return 0;
    }

    char name_buf[256];

    while (*p) {
        const char *end = find_next_slash(p);
        u32 seg_len;
        if (end) {
            seg_len = (u32)(end - p);
        } else {
            seg_len = strlen(p);
        }

        if (seg_len == 0) break;
        if (seg_len >= sizeof(name_buf)) return -1;

        memcpy(name_buf, p, seg_len);
        name_buf[seg_len] = '\0';

        Inode dir_inode;
        if (dir_inode.read_from_disk(*m_disk, m_sb, m_bgt, current_ino) != 0)
            return -1;
        if (!dir_inode.is_dir())
            return -1;

        Directory dir;
        if (dir.read(*m_disk, m_sb, dir_inode) != 0)
            return -1;

        u32 child_ino = 0;
        if (dir.lookup(name_buf, &child_ino) != 0)
            return -1;

        current_ino = child_ino;
        p = end ? skip_slash(end) : "";
    }

    *out_ino = current_ino;
    return 0;
}

int FileSystem::read_inode(u32 ino, Inode *out)
{
    return out->read_from_disk(*m_disk, m_sb, m_bgt, ino);
}

int FileSystem::read_block(u32 blk_no, void *buf)
{
    return m_disk->read_block(blk_no, m_sb.block_size(), buf);
}

int FileSystem::alloc_fd()
{
    for (u32 i = 0; i < MAX_OPEN_FILES; i++) {
        if (!m_files[i].in_use) {
            m_files[i].in_use = true;
            m_files[i].pos = 0;
            return (int)i;
        }
    }
    return -1;
}

int FileSystem::open(const char *path, u32 flags)
{
    (void)flags;
    if (!m_mounted) return -1;

    u32 ino = 0;
    if (resolve_path(path, &ino) != 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "fs: path not found: %s", path);
        return -1;
    }

    int fd = alloc_fd();
    if (fd < 0) {
        klog_lvl(KLOG_ERR, "bsd.ext3", "fs: too many open files");
        return -1;
    }

    File &f = m_files[fd];
    f.inode_no = ino;
    if (read_inode(ino, &f.inode) != 0) {
        f.in_use = false;
        return -1;
    }
    f.pos = 0;

    klog("bsd.ext3", "fs: opened '%s' (fd=%d, ino=%u)", path, fd, ino);
    return fd;
}

int FileSystem::close(int fd)
{
    if (fd < 0 || (u32)fd >= MAX_OPEN_FILES) return -1;
    if (!m_files[fd].in_use) return -1;

    m_files[fd].in_use = false;
    m_files[fd].inode_no = 0;
    m_files[fd].pos = 0;

    return 0;
}

int FileSystem::read(int fd, void *buf, u32 len)
{
    if (fd < 0 || (u32)fd >= MAX_OPEN_FILES) return -1;
    if (!m_files[fd].in_use) return -1;

    File &f = m_files[fd];
    u32 file_size = f.inode.size();
    if (f.pos >= file_size) return 0;

    if (f.pos + len > file_size)
        len = file_size - f.pos;

    int bytes = f.inode.read_data(*m_disk, m_sb, f.pos, len, buf);
    if (bytes > 0)
        f.pos += bytes;

    return bytes;
}

int FileSystem::seek(int fd, s32 offset, int whence)
{
    if (fd < 0 || (u32)fd >= MAX_OPEN_FILES) return -1;
    if (!m_files[fd].in_use) return -1;

    File &f = m_files[fd];
    u32 file_size = f.inode.size();
    u32 new_pos = f.pos;

    switch (whence) {
    case 0: /* SEEK_SET */
        new_pos = (u32)offset;
        break;
    case 1: /* SEEK_CUR */
        new_pos = f.pos + offset;
        break;
    case 2: /* SEEK_END */
        if ((u32)(-offset) > file_size) return -1;
        new_pos = file_size - (u32)(-offset);
        break;
    default:
        return -1;
    }

    if (new_pos > file_size)
        return -1;

    f.pos = new_pos;
    return 0;
}

int FileSystem::stat(const char *path, struct xkern_stat *st)
{
    if (!m_mounted || !st) return -1;

    u32 ino = 0;
    if (resolve_path(path, &ino) != 0)
        return -1;

    Inode inode;
    if (read_inode(ino, &inode) != 0)
        return -1;

    memset(st, 0, sizeof(*st));
    st->st_ino = ino;
    st->st_mode = inode.mode();
    st->st_nlink = inode.links();
    st->st_uid = inode.uid();
    st->st_gid = inode.gid();
    st->st_size = inode.size();
    st->st_blksize = m_sb.block_size();
    st->st_blocks = inode.blocks();
    st->st_atime = inode.atime();
    st->st_mtime = inode.mtime();
    st->st_ctime = inode.ctime();

    return 0;
}

int FileSystem::fstat(int fd, struct xkern_stat *st)
{
    if (fd < 0 || (u32)fd >= MAX_OPEN_FILES) return -1;
    if (!m_files[fd].in_use || !st) return -1;

    File &f = m_files[fd];
    memset(st, 0, sizeof(*st));
    st->st_ino = f.inode_no;
    st->st_mode = f.inode.mode();
    st->st_nlink = f.inode.links();
    st->st_uid = f.inode.uid();
    st->st_gid = f.inode.gid();
    st->st_size = f.inode.size();
    st->st_blksize = m_sb.block_size();
    st->st_blocks = f.inode.blocks();
    st->st_atime = f.inode.atime();
    st->st_mtime = f.inode.mtime();
    st->st_ctime = f.inode.ctime();

    return 0;
}

int FileSystem::getdents(const char *path, u32 index, char *name, u32 namelen)
{
    if (!m_mounted || !name) return -1;

    u32 ino = 0;
    if (resolve_path(path, &ino) != 0)
        return -1;

    Inode dir_inode;
    if (read_inode(ino, &dir_inode) != 0)
        return -1;
    if (!dir_inode.is_dir())
        return -1;

    Directory dir;
    if (dir.read(*m_disk, m_sb, dir_inode) != 0)
        return -1;

    DirEntry entry;
    if (dir.get_entry(index, &entry) != 0)
        return -1;

    u32 copy_len = entry.name_len;
    if (copy_len >= namelen) copy_len = namelen - 1;
    memcpy(name, entry.name, copy_len);
    name[copy_len] = '\0';

    return 0;
}

void FileSystem::dump() const
{
    if (!m_mounted) {
        klog("bsd.ext3", "fs: not mounted");
        return;
    }
    klog("bsd.ext3", "fs: mounted on '%s'", m_dev);
    m_sb.dump();
    m_bgt.dump();
}

FileSystem *ext3_get_fs()
{
    return g_fs_instance;
}

} // namespace ext3
