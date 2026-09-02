#pragma once

#include "types.h"
#include "syscall.h"

extern "C" {
#include "string.h"
#include "klibc.h"
#include "klog.h"
}

constexpr int RAMFS_MAX_ENTRIES = 128;
constexpr int RAMFS_NAME_LEN    = 64;

enum class FileType : u8 {
    Free    = 0,
    Regular = 1,
    Console = 2,
};

struct FileEntry {
    char     name[RAMFS_NAME_LEN];
    u8      *data;
    u32      size;
    FileType type;
};

class RamFS {
public:
    RamFS();

    void       reset();
    int        add(const char *name, const u8 *data, u32 size);
    FileEntry *lookup(const char *path);
    int        remove(const char *name);
    void       list() const;

    int        unpack_cpio(u8 *archive, u32 length);

    int        count() const { return m_count; }
    FileEntry *entries() { return m_files; }
    bool       empty() const { return m_count == 0; }
    bool       full() const { return m_count >= RAMFS_MAX_ENTRIES; }

    FileEntry *begin() { return m_files; }
    FileEntry *end()   { return m_files + m_count; }
    const FileEntry *begin() const { return m_files; }
    const FileEntry *end() const   { return m_files + m_count; }

private:
    static void sanitize_name(const char *src, char *dst);

    FileEntry m_files[RAMFS_MAX_ENTRIES];
    int       m_count;
};

RamFS *ramfs_global();
void   ramfs_set_global(RamFS *fs);

bool cpio_valid_magic(const char *header);
