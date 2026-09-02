#include "include/ramfs.hpp"

enum {
    CPIO_NEWC_HEADER_SIZE = 110,
};

static u32 cpio_hex(const char *s, int len)
{
    u32 v = 0;

    for (int i = 0; i < len; i++) {
        char c = s[i];
        v = v * 16 + ((c >= '0' && c <= '9') ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return v;
}

static bool valid_cpio_magic(const char *h)
{
    return h[0] == '0' && h[1] == '7' && h[2] == '0' &&
           h[3] == '7' && h[4] == '0' && h[5] == '1';
}

static bool valid_hex_field(const char *s, int len)
{
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

static bool valid_cpio_name(const char *name, u32 namesize)
{
    if (namesize < 2 || name[namesize - 1] != '\0')
        return false;

    for (u32 i = 0; i < namesize - 1; i++) {
        if (name[i] == '\0' || name[i] == '\n' ||
            name[i] == '\r' || name[i] == '\t')
            return false;
    }

    return static_cast<int>(namesize) <= RAMFS_NAME_LEN;
}

bool cpio_valid_magic(const char *header)
{
    return valid_cpio_magic(header);
}

int RamFS::unpack_cpio(u8 *archive, u32 length)
{
    u32 offset = 0;

    m_count = 0;

    while (offset + CPIO_NEWC_HEADER_SIZE <= length) {
        char *h = reinterpret_cast<char *>(archive + offset);
        char name[RAMFS_NAME_LEN];
        u32 filesize, namesize, data_off;

        if (!valid_cpio_magic(h) || !valid_hex_field(h + 6, 104))
            return -1;

        filesize = cpio_hex(h + 54, 8);
        namesize = cpio_hex(h + 94, 8);

        if (namesize < 2 || namesize >= RAMFS_NAME_LEN)
            return -1;
        if (!valid_cpio_name(reinterpret_cast<const char *>(archive + offset + CPIO_NEWC_HEADER_SIZE), namesize))
            return -1;

        for (u32 i = 0; i + 1 < namesize; i++)
            name[i] = static_cast<char>(archive[offset + CPIO_NEWC_HEADER_SIZE + i]);
        name[namesize - 1] = '\0';

        if (klibc.strcmp(name, "TRAILER!!!") == 0)
            break;

        data_off = ((offset + CPIO_NEWC_HEADER_SIZE + namesize + 3) & ~3u);
        if (data_off + filesize > length)
            return -1;

        if (name[0] == '.' && (name[1] == '\0' || name[1] == '/'))
            goto next;

        add(name, archive + data_off, filesize);

    next:
        offset = ((data_off + filesize + 3) & ~3u);
    }

    return 0;
}
