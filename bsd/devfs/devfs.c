#include "devfs.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include "klibc.h"

#define S_IFMT   0xF000
#define S_IFDIR  0x4000
#define S_IFBLK  0x6000
#define S_IFCHR  0x2000
#define S_IFREG  0x8000
#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRGRP  0040
#define S_IROTH  0004

static struct devfs_node g_nodes[DEVFS_MAX_NODES];
static u32 g_node_count;
static u32 g_next_id = 1;

static const char *devclass_name(enum devfs_dev_class dc)
{
    switch (dc) {
    case DEVFS_CLASS_CPU:        return "cpu";
    case DEVFS_CLASS_STORAGE:    return "storage";
    case DEVFS_CLASS_USB:        return "usb";
    case DEVFS_CLASS_HID_KBD:    return "kbd";
    case DEVFS_CLASS_HID_MOUSE:  return "mouse";
    case DEVFS_CLASS_GRAPHICS:   return "graphics";
    case DEVFS_CLASS_AUDIO:      return "audio";
    case DEVFS_CLASS_NETWORK:    return "network";
    case DEVFS_CLASS_SERIAL:     return "serial";
    case DEVFS_CLASS_PCI:        return "pci";
    case DEVFS_CLASS_FRAMEBUFFER:return "fb";
    default:                     return "unknown";
    }
}

static const char *nodetype_name(enum devfs_node_type t)
{
    switch (t) {
    case DEVFS_NODE_DIR:     return "dir";
    case DEVFS_NODE_BLOCK:   return "blk";
    case DEVFS_NODE_CHAR:    return "chr";
    case DEVFS_NODE_SYMLINK: return "lnk";
    default:                 return "?";
    }
}

/* strip leading slashes and resolve ".." / "." segments */
static void normalize_path(const char *in, char *out, u32 out_sz)
{
    u32 w = 0;
    const char *p = in;

    while (*p == '/')
        p++;

    while (*p && w < out_sz - 1) {
        if (*p == '/') {
            out[w++] = '/';
            p++;
            while (*p == '/')
                p++;
            continue;
        }
        if (p[0] == '.' && p[1] == '/') {
            p += 2;
            continue;
        }
        if (p[0] == '.' && p[1] == '.' && p[2] == '/') {
            /* back up one component */
            if (w > 0) {
                w--;
                while (w > 0 && out[w - 1] != '/')
                    w--;
            }
            p += 3;
            continue;
        }
        out[w++] = *p++;
    }
    if (w == 0) {
        out[w++] = '/';
    } else if (out[w - 1] == '/') {
        /* trailing slash only for root */
        if (w == 1)
            ;
        else
            w--;
    }
    out[w] = '\0';
}

/* return pointer to the last component of a normalized path */
static const char *path_leaf(const char *path)
{
    const char *p = path;
    const char *last = path;

    while (*p) {
        if (*p == '/')
            last = p + 1;
        p++;
    }
    return last;
}

/* find a direct child of parent_idx with the given name */
static struct devfs_node *find_child(u32 parent_idx, const char *name)
{
    struct devfs_node *parent = &g_nodes[parent_idx];
    u32 i;

    for (i = 0; i < parent->child_count; i++) {
        struct devfs_node *child = &g_nodes[parent->children[i]];
        if (klibc.strcmp(child->name, name) == 0)
            return child;
    }
    return 0;
}

/* ---- core implementation ---- */

int devfs_init(void)
{
    struct devfs_node root;

    g_node_count = 0;
    g_next_id = 1;

    klibc.memset(g_nodes, 0, sizeof(g_nodes));

    /* create the root node /dev */
    klibc.strncpy(root.name, "dev", BSD_DEVFS_NAME_MAX - 1);
    root.name[BSD_DEVFS_NAME_MAX - 1] = '\0';
    root.type = DEVFS_NODE_DIR;
    root.devclass = DEVFS_CLASS_UNKNOWN;
    root.mode = 0755;
    root.size = 0;
    root.rdev = 0;
    root.ops = 0;
    root.priv = 0;
    root.parent = 0;
    root.child_count = 0;
    root.id = g_next_id++;

    g_nodes[0] = root;
    g_node_count = 1;

    klog("bsd.devfs", "devfs_init() root=/dev, %u node slots", DEVFS_MAX_NODES);
    return 0;
}

int devfs_node_add(struct devfs_node *node)
{
    char norm[DEVFS_PATH_MAX];
    char parent_path[DEVFS_PATH_MAX];
    const char *leaf;
    struct devfs_node *parent;
    u32 parent_idx;
    u32 slot;

    if (g_node_count >= DEVFS_MAX_NODES) {
        klog_lvl(KLOG_ERR, "bsd.devfs", "node table full");
        return -1;
    }

    normalize_path(node->name, norm, DEVFS_PATH_MAX);
    klibc.strncpy(node->name, norm, BSD_DEVFS_NAME_MAX - 1);
    node->name[BSD_DEVFS_NAME_MAX - 1] = '\0';

    leaf = path_leaf(norm);

    /* build parent path */
    {
        u32 len = (u32)klibc.strlen(norm);
        u32 i;

        if (len >= DEVFS_PATH_MAX)
            return -1;
        klibc.memcpy(parent_path, norm, len + 1);
        /* remove trailing leaf */
        if (len > 1) {
            i = len - 1;
            while (i > 0 && parent_path[i - 1] != '/')
                i--;
            parent_path[i] = '\0';
        } else {
            parent_path[0] = '/';
            parent_path[1] = '\0';
        }
    }

    /* find parent -- root node IS the base, so "/" or matching root name => parent is root */
    parent_idx = 0;
    if (klibc.strcmp(parent_path, "/") != 0) {
        char *seg;
        char tmp[DEVFS_PATH_MAX];
        int root_matched = 0;

        klibc.strncpy(tmp, parent_path, DEVFS_PATH_MAX - 1);
        tmp[DEVFS_PATH_MAX - 1] = '\0';

        seg = tmp;
        while (*seg == '/')
            seg++;

        /* check if the first segment matches the root node name */
        if (*seg) {
            char component[BSD_DEVFS_NAME_MAX];
            u32 ci = 0;
            const char *s = seg;

            while (*s && *s != '/' && ci < BSD_DEVFS_NAME_MAX - 1)
                component[ci++] = *s++;
            component[ci] = '\0';

            if (klibc.strcmp(component, g_nodes[0].name) == 0) {
                parent_idx = 0;
                seg = (char *)s;
                while (*seg == '/')
                    seg++;
                root_matched = 1;
            }
        }

        while (*seg) {
            char component[BSD_DEVFS_NAME_MAX];
            u32 ci = 0;

            while (*seg && *seg != '/' && ci < BSD_DEVFS_NAME_MAX - 1)
                component[ci++] = *seg++;
            component[ci] = '\0';

            parent = find_child(parent_idx, component);
            if (!parent || parent->type != DEVFS_NODE_DIR) {
                klog_lvl(KLOG_ERR, "bsd.devfs", "parent not found for %s", norm);
                return -1;
            }
            parent_idx = (u32)(parent - g_nodes);

            while (*seg == '/')
                seg++;
        }
        (void)root_matched;
    }

    /* check for duplicates */
    if (find_child(parent_idx, leaf)) {
        klog_lvl(KLOG_WARNING, "bsd.devfs", "duplicate node: %s", norm);
        return -1;
    }

    /* allocate slot */
    slot = g_node_count++;
    klibc.memset(&g_nodes[slot], 0, sizeof(struct devfs_node));

    klibc.strncpy(g_nodes[slot].name, leaf, BSD_DEVFS_NAME_MAX - 1);
    g_nodes[slot].name[BSD_DEVFS_NAME_MAX - 1] = '\0';
    g_nodes[slot].type = node->type;
    g_nodes[slot].devclass = node->devclass;
    g_nodes[slot].mode = node->mode;
    g_nodes[slot].size = node->size;
    g_nodes[slot].rdev = node->rdev;
    g_nodes[slot].ops = node->ops;
    g_nodes[slot].priv = node->priv;
    g_nodes[slot].parent = &g_nodes[parent_idx];
    g_nodes[slot].child_count = 0;
    g_nodes[slot].id = g_next_id++;

    /* link into parent */
    parent = &g_nodes[parent_idx];
    if (parent->child_count < DEVFS_MAX_CHILDREN) {
        parent->children[parent->child_count++] = slot;
    } else {
        klog_lvl(KLOG_ERR, "bsd.devfs", "parent %s child table full", parent->name);
        g_node_count--;
        return -1;
    }

   // klog("bsd.devfs", "add %s (%s, %s)", norm,
     //    nodetype_name(g_nodes[slot].type),
       //  devclass_name(g_nodes[slot].devclass));
    return 0;
}

int devfs_node_remove(const char *path)
{
    char norm[DEVFS_PATH_MAX];
    struct devfs_node *node;
    u32 idx, parent_idx, i, j;

    normalize_path(path, norm, DEVFS_PATH_MAX);

    node = devfs_node_find(norm);
    if (!node) {
        klog_lvl(KLOG_ERR, "bsd.devfs", "remove: %s not found", norm);
        return -1;
    }

    idx = (u32)(node - g_nodes);
    if (idx == 0) {
        klog_lvl(KLOG_ERR, "bsd.devfs", "cannot remove root");
        return -1;
    }

    /* remove from parent's child list */
    parent_idx = (u32)(node->parent - g_nodes);
    for (i = 0; i < g_nodes[parent_idx].child_count; i++) {
        if (g_nodes[parent_idx].children[i] == idx) {
            for (j = i; j < g_nodes[parent_idx].child_count - 1; j++)
                g_nodes[parent_idx].children[j] = g_nodes[parent_idx].children[j + 1];
            g_nodes[parent_idx].child_count--;
            break;
        }
    }

    klog("bsd.devfs", "remove %s", norm);
    return 0;
}

struct devfs_node *devfs_node_find(const char *path)
{
    char norm[DEVFS_PATH_MAX];
    const char *seg;
    u32 cur;

    normalize_path(path, norm, DEVFS_PATH_MAX);

    /* root */
    if (klibc.strcmp(norm, "/") == 0 || klibc.strcmp(norm, "/dev") == 0)
        return &g_nodes[0];

    cur = 0;
    seg = norm;
    while (*seg == '/')
        seg++;

    /* skip first segment if it matches root node name */
    if (*seg) {
        char component[BSD_DEVFS_NAME_MAX];
        u32 ci = 0;
        const char *s = seg;

        while (*s && *s != '/' && ci < BSD_DEVFS_NAME_MAX - 1)
            component[ci++] = *s++;
        component[ci] = '\0';

        if (klibc.strcmp(component, g_nodes[0].name) == 0) {
            seg = s;
            while (*seg == '/')
                seg++;
        }
    }

    while (*seg) {
        char component[BSD_DEVFS_NAME_MAX];
        u32 ci = 0;
        struct devfs_node *child;

        while (*seg && *seg != '/' && ci < BSD_DEVFS_NAME_MAX - 1)
            component[ci++] = *seg++;
        component[ci] = '\0';

        child = find_child(cur, component);
        if (!child)
            return 0;

        cur = (u32)(child - g_nodes);

        while (*seg == '/')
            seg++;
    }

    return &g_nodes[cur];
}

struct devfs_node *devfs_node_find_id(u32 id)
{
    u32 i;

    for (i = 0; i < g_node_count; i++) {
        if (g_nodes[i].id == id)
            return &g_nodes[i];
    }
    return 0;
}

u32 devfs_node_count(void)
{
    return g_node_count;
}

int devfs_mkdir(const char *path)
{
    struct devfs_node node;

    klibc.memset(&node, 0, sizeof(node));
    klibc.strncpy(node.name, path, BSD_DEVFS_NAME_MAX - 1);
    node.name[BSD_DEVFS_NAME_MAX - 1] = '\0';
    node.type = DEVFS_NODE_DIR;
    node.devclass = DEVFS_CLASS_UNKNOWN;
    node.mode = 0755;

    return devfs_node_add(&node);
}

int devfs_add_device(const char *path, enum devfs_node_type type,
                     struct devfs_ops *ops, void *priv)
{
    struct devfs_node node;

    klibc.memset(&node, 0, sizeof(node));
    klibc.strncpy(node.name, path, BSD_DEVFS_NAME_MAX - 1);
    node.name[BSD_DEVFS_NAME_MAX - 1] = '\0';
    node.type = type;
    node.devclass = ops ? ops->devclass : DEVFS_CLASS_UNKNOWN;
    node.mode = (type == DEVFS_NODE_BLOCK) ? 0660 : 0660;
    node.ops = ops;
    node.priv = priv;

    return devfs_node_add(&node);
}

/* ---- default opset implementation ---- */

static int default_add(struct devfs_node *node)
{
    return devfs_node_add(node);
}

static int default_remove(const char *path)
{
    return devfs_node_remove(path);
}

static struct devfs_node *default_find(const char *path)
{
    return devfs_node_find(path);
}

static int default_open(struct devfs_node *node)
{
    if (node && node->ops && node->ops->open)
        return node->ops->open(node);
    return 0;
}

static int default_close(struct devfs_node *node)
{
    if (node && node->ops && node->ops->close)
        return node->ops->close(node);
    return 0;
}

static int default_read(struct devfs_node *node, u32 off, void *buf, u32 len)
{
    if (node && node->ops && node->ops->read)
        return node->ops->read(node, off, buf, len);
    return -1;
}

static int default_write(struct devfs_node *node, u32 off,
                         const void *buf, u32 len)
{
    if (node && node->ops && node->ops->write)
        return node->ops->write(node, off, buf, len);
    return -1;
}

static int default_stat(struct devfs_node *node, struct xkern_stat *st)
{
    klibc.memset(st, 0, sizeof(*st));
    if (!node)
        return -1;
    st->st_ino = node->id;
    st->st_nlink = 1;
    st->st_blksize = 4096;
    switch (node->type) {
    case DEVFS_NODE_DIR:
        st->st_mode = S_IFDIR | node->mode;
        st->st_size = 0;
        break;
    case DEVFS_NODE_BLOCK:
        st->st_mode = S_IFBLK | node->mode;
        st->st_rdev = node->rdev;
        st->st_size = node->size;
        st->st_blocks = node->size ? (node->size + 511) / 512 : 0;
        break;
    case DEVFS_NODE_CHAR:
        st->st_mode = S_IFCHR | node->mode;
        st->st_rdev = node->rdev;
        st->st_size = node->size;
        st->st_blocks = node->size ? (node->size + 511) / 512 : 0;
        break;
    default:
        st->st_mode = S_IFREG | node->mode;
        break;
    }
    return 0;
}

static int default_getdents(const char *path, u32 index,
                            char *name, u32 namelen)
{
    struct devfs_node *dir;
    u32 i;

    dir = devfs_node_find(path);
    if (!dir || dir->type != DEVFS_NODE_DIR)
        return 0;

    if (index >= dir->child_count)
        return 0;

    i = dir->children[index];
    {
        struct devfs_node *child = &g_nodes[i];
        u32 len = (u32)klibc.strlen(child->name);

        if (len >= namelen)
            len = namelen - 1;
        klibc.memcpy(name, child->name, len);
        name[len] = '\0';
    }
    return (g_nodes[i].type == DEVFS_NODE_DIR) ? XKERN_DT_DIR : XKERN_DT_REG;
}

struct devfs_opset g_devfs_opset = {
    .add      = default_add,
    .remove   = default_remove,
    .find     = default_find,
    .open     = default_open,
    .close    = default_close,
    .read     = default_read,
    .write    = default_write,
    .stat     = default_stat,
    .getdents = default_getdents,
};

/* ---- VFS integration ---- */

int devfs_vfs_open(const char *path, u32 flags)
{
    struct devfs_node *node;

    (void)flags;
    node = devfs_node_find(path);
    if (!node)
        return -1;
    if (node->type == DEVFS_NODE_DIR)
        return -2;  /* directories can't be opened as files */
    return default_open(node);
}

int devfs_vfs_close(int fd)
{
    struct devfs_node *node = devfs_node_find_id((u32)fd);

    if (!node)
        return -1;
    return default_close(node);
}

int devfs_vfs_read(int fd, void *buf, u32 n)
{
    struct devfs_node *node = devfs_node_find_id((u32)fd);

    if (!node)
        return -1;
    return default_read(node, 0, buf, n);
}

int devfs_vfs_write(int fd, const void *buf, u32 n)
{
    struct devfs_node *node = devfs_node_find_id((u32)fd);

    if (!node)
        return -1;
    return default_write(node, 0, buf, n);
}

int devfs_vfs_stat(const char *path, struct xkern_stat *st)
{
    struct devfs_node *node = devfs_node_find(path);

    if (!node)
        return -1;
    return default_stat(node, st);
}

int devfs_vfs_getdents(const char *path, u32 index,
                       char *name, u32 namelen)
{
    return default_getdents(path, index, name, namelen);
}

/* ---- debug tree dump ---- */

static void dump_node(u32 idx, const char *prefix, int is_last)
{
    struct devfs_node *n = &g_nodes[idx];
    char branch[48];
    char child_prefix[48];
    u32 i;
    int next_is_last;

    /* build this node's connector */
    klibc.strncpy(branch, prefix, sizeof(branch) - 1);
    branch[sizeof(branch) - 1] = '\0';
    {
        u32 len = (u32)klibc.strlen(branch);
        if (len < sizeof(branch) - 4) {
            klibc.strcpy(branch + len, is_last ? "`-- " : "|-- ");
        }
    }

    /* build prefix for children */
    klibc.strncpy(child_prefix, prefix, sizeof(child_prefix) - 1);
    child_prefix[sizeof(child_prefix) - 1] = '\0';
    {
        u32 len = (u32)klibc.strlen(child_prefix);
        if (len < sizeof(child_prefix) - 4) {
            klibc.strcpy(child_prefix + len, is_last ? "    " : "|   ");
        }
    }

    /* print this node */
    switch (n->type) {
    case DEVFS_NODE_DIR:
        klog("bsd.devfs", "%s%s/", branch, n->name);
        break;
    case DEVFS_NODE_BLOCK:
        klog("bsd.devfs", "%s%s  [blk %s]", branch, n->name,
             devclass_name(n->devclass));
        break;
    case DEVFS_NODE_CHAR:
        klog("bsd.devfs", "%s%s  [chr %s]", branch, n->name,
             devclass_name(n->devclass));
        break;
    case DEVFS_NODE_SYMLINK:
        klog("bsd.devfs", "%s%s  [lnk]", branch, n->name);
        break;
    default:
        klog("bsd.devfs", "%s%s  [?]", branch, n->name);
        break;
    }

    /* recurse into children */
    for (i = 0; i < n->child_count; i++) {
        next_is_last = (i == n->child_count - 1);
        dump_node(n->children[i], child_prefix, next_is_last);
    }
}

void devfs_dump(void)
{
    klog("bsd.devfs", "devfs tree (%u nodes):", g_node_count);
    dump_node(0, "", 1);
}
