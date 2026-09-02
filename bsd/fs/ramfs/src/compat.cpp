#include "include/ramfs_compat.h"
#include "include/ramfs.hpp"

static struct ramfs s_compat_fs;

void ramfs_add(struct ramfs *fs, const char *name, u8 *data, u32 size)
{
    (void)fs;

    if (s_compat_fs.count >= RAMFS_MAX_FILES)
        return;

    struct ramfs_file *f = &s_compat_fs.files[s_compat_fs.count];

    const char *p = name;
    while (*p == '/')
        p++;
    while (p[0] == '.' && p[1] == '/')
        p += 2;
    while (*p == '/')
        p++;

    klibc.strncpy(f->name, p, RAMFS_NAME_MAX - 1);
    f->name[RAMFS_NAME_MAX - 1] = '\0';
    f->data = data;
    f->size = size;
    s_compat_fs.count++;
}

struct ramfs_file *ramfs_lookup(struct ramfs *fs, const char *path)
{
    (void)fs;

    const char *p = path;
    while (*p == '/')
        p++;

    for (int i = 0; i < s_compat_fs.count; i++) {
        if (klibc.strcmp(s_compat_fs.files[i].name, p) == 0)
            return &s_compat_fs.files[i];
    }
    return nullptr;
}

struct ramfs *ramfs_get(void)
{
    return &s_compat_fs;
}

void ramfs_list(struct ramfs *fs)
{
    (void)fs;
    klog("ramfs", "ramfs: %u files", s_compat_fs.count);
    for (int i = 0; i < s_compat_fs.count; i++) {
        klog("ramfs", "  /%s (%u bytes)",
             s_compat_fs.files[i].name, s_compat_fs.files[i].size);
    }
}

int cpio_unpack(u8 *arc, u32 len, struct ramfs *fs)
{
    (void)fs;

    u32 offset = 0;
    s_compat_fs.count = 0;

    enum { CPIO_NEWC_HDR = 110 };

    auto hex = [](const char *s, int n) -> u32 {
        u32 v = 0;
        for (int i = 0; i < n; i++) {
            char c = s[i];
            v = v * 16 + ((c >= '0' && c <= '9') ? c - '0' : (c | 0x20) - 'a' + 10);
        }
        return v;
    };

    while (offset + CPIO_NEWC_HDR <= len) {
        char *h = reinterpret_cast<char *>(arc + offset);
        u32 filesize = hex(h + 54, 8);
        u32 namesize = hex(h + 94, 8);

        if (namesize < 2 || namesize >= RAMFS_NAME_MAX)
            return -1;

        char name[RAMFS_NAME_MAX];
        for (u32 i = 0; i + 1 < namesize; i++)
            name[i] = static_cast<char>(arc[offset + CPIO_NEWC_HDR + i]);
        name[namesize - 1] = '\0';

        if (klibc.strcmp(name, "TRAILER!!!") == 0)
            break;

        u32 data_off = ((offset + CPIO_NEWC_HDR + namesize + 3) & ~3u);
        if (data_off + filesize > len)
            return -1;

        if (name[0] == '.' && (name[1] == '\0' || name[1] == '/'))
            goto next;

        ramfs_add(nullptr, name, arc + data_off, filesize);

    next:
        offset = ((data_off + filesize + 3) & ~3u);
    }

    return 0;
}

int valid_cpio_magic(const char *h)
{
    return h[0] == '0' && h[1] == '7' && h[2] == '0' &&
           h[3] == '7' && h[4] == '0' && h[5] == '1';
}
