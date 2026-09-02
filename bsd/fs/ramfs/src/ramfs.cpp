#include "include/ramfs.hpp"

static RamFS *g_instance = nullptr;

RamFS::RamFS()
    : m_count(0)
{
    for (int i = 0; i < RAMFS_MAX_ENTRIES; i++) {
        m_files[i].name[0] = '\0';
        m_files[i].data = nullptr;
        m_files[i].size = 0;
        m_files[i].type = FileType::Free;
    }
}

void RamFS::reset()
{
    m_count = 0;
    for (int i = 0; i < RAMFS_MAX_ENTRIES; i++) {
        m_files[i].name[0] = '\0';
        m_files[i].data = nullptr;
        m_files[i].size = 0;
        m_files[i].type = FileType::Free;
    }
}

void RamFS::sanitize_name(const char *src, char *dst)
{
    while (*src == '/')
        src++;
    while (src[0] == '.' && src[1] == '/')
        src += 2;
    while (*src == '/')
        src++;
    klibc.strncpy(dst, src, RAMFS_NAME_LEN - 1);
    dst[RAMFS_NAME_LEN - 1] = '\0';
}

int RamFS::add(const char *name, const u8 *data, u32 size)
{
    if (m_count >= RAMFS_MAX_ENTRIES)
        return -1;

    FileEntry &f = m_files[m_count];
    sanitize_name(name, f.name);
    f.data = const_cast<u8 *>(data);
    f.size = size;
    f.type = FileType::Regular;
    m_count++;

    return 0;
}

FileEntry *RamFS::lookup(const char *path)
{
    const char *p = path;

    while (*p == '/')
        p++;

    for (int i = 0; i < m_count; i++) {
        if (klibc.strcmp(m_files[i].name, p) == 0)
            return &m_files[i];
    }
    return nullptr;
}

int RamFS::remove(const char *name)
{
    const char *p = name;

    while (*p == '/')
        p++;

    for (int i = 0; i < m_count; i++) {
        if (klibc.strcmp(m_files[i].name, p) == 0) {
            if (i < m_count - 1) {
                klibc.memcpy(&m_files[i], &m_files[i + 1],
                             sizeof(FileEntry) * (m_count - 1 - i));
            }
            m_count--;
            m_files[m_count].name[0] = '\0';
            m_files[m_count].data = nullptr;
            m_files[m_count].size = 0;
            m_files[m_count].type = FileType::Free;
            return 0;
        }
    }
    return -1;
}

void RamFS::list() const
{
    klog("ramfs", "ramfs: %u files", m_count);
    for (int i = 0; i < m_count; i++) {
        klog("ramfs", "  /%s (%u bytes)", m_files[i].name, m_files[i].size);
    }
}

RamFS *ramfs_global()
{
    return g_instance;
}

void ramfs_set_global(RamFS *fs)
{
    g_instance = fs;
}
