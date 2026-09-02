#pragma once

#include "types.h"
#include "syscall.h"
#include "ext3/include/superblock.hpp"
#include "ext3/include/blk_group.hpp"
#include "ext3/include/inode.hpp"
#include "ext3/include/dir.hpp"

namespace ext3 {

class Disk;

struct File {
    u32  inode_no;
    Inode inode;
    u32  pos;
    bool in_use;
};

class FileSystem {
public:
    FileSystem();

    int  mount(Disk *disk, const char *dev_name);
    int  unmount();
    bool mounted() const { return m_mounted; }

    int  open(const char *path, u32 flags);
    int  close(int fd);
    int  read(int fd, void *buf, u32 len);
    int  seek(int fd, s32 offset, int whence);

    int  stat(const char *path, struct xkern_stat *st);
    int  fstat(int fd, struct xkern_stat *st);
    int  getdents(const char *path, u32 index, char *name, u32 namelen);

    void dump() const;

private:
    int  resolve_path(const char *path, u32 *out_ino);
    int  alloc_fd();
    int  read_inode(u32 ino, Inode *out);
    int  read_block(u32 blk_no, void *buf);

    Superblock            m_sb;
    BlockGroupDescriptorTable m_bgt;
    Disk                 *m_disk;
    bool                  m_mounted;
    char                  m_dev[24];

    static const u32      MAX_OPEN_FILES = 16;
    File                  m_files[MAX_OPEN_FILES];
};

FileSystem *ext3_get_fs();

} // namespace ext3
