#pragma once

#include "types.h"
#include "ext3/include/const.hpp"

namespace ext3 {

#pragma pack(push, 1)
struct dir_entry_disk {
    u32 inode;
    u16 rec_len;
    u8  name_len;
    u8  file_type;
    char name[EXT3_NAME_LEN];
};
#pragma pack(pop)

struct DirEntry {
    u32 inode;
    u16 rec_len;
    u8  name_len;
    u8  file_type;
    char name[EXT3_NAME_LEN + 1];
};

class Disk;
class Superblock;
class Inode;

class Directory {
public:
    Directory();

    int  read(class Disk &disk, const Superblock &sb, const Inode &inode);
    void free();
    void dump() const;

    int  lookup(const char *name, u32 *out_ino) const;
    int  get_entry(u32 index, DirEntry *out) const;
    u32  entry_count() const { return m_entry_count; }
    u32  size() const { return m_size; }

private:
    u8          *m_data;
    u32          m_size;
    u32          m_entry_count;
};

} // namespace ext3
