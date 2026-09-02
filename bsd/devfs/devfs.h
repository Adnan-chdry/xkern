#ifndef BSD_DEVFS_H
#define BSD_DEVFS_H

#include "types.h"
#include "syscall.h"

#define DEVFS_PATH_MAX    64
#define BSD_DEVFS_NAME_MAX 32
#define DEVFS_MAX_NODES   128
#define DEVFS_MAX_CHILDREN 32

/* node types */
enum devfs_node_type {
    DEVFS_NODE_DIR,
    DEVFS_NODE_BLOCK,
    DEVFS_NODE_CHAR,
    DEVFS_NODE_SYMLINK,
};

/* device classes (for scanner categorisation) */
enum devfs_dev_class {
    DEVFS_CLASS_UNKNOWN,
    DEVFS_CLASS_CPU,
    DEVFS_CLASS_STORAGE,
    DEVFS_CLASS_USB,
    DEVFS_CLASS_HID_KBD,
    DEVFS_CLASS_HID_MOUSE,
    DEVFS_CLASS_GRAPHICS,
    DEVFS_CLASS_AUDIO,
    DEVFS_CLASS_NETWORK,
    DEVFS_CLASS_SERIAL,
    DEVFS_CLASS_PCI,
    DEVFS_CLASS_FRAMEBUFFER,
};

struct devfs_node;

/*
 * Per-device operations table.
 * Every device class registers one of these so devfs knows
 * how to open / read / write / close a node it owns.
 */
struct devfs_ops {
    const char *name;
    enum devfs_dev_class devclass;
    int  (*open)(struct devfs_node *node);
    int  (*close)(struct devfs_node *node);
    int  (*read)(struct devfs_node *node, u32 off, void *buf, u32 len);
    int  (*write)(struct devfs_node *node, u32 off, const void *buf, u32 len);
    int  (*ioctl)(struct devfs_node *node, u32 cmd, u64 arg);
    void (*poll_notify)(struct devfs_node *node);
};

struct devfs_node {
    char                         name[BSD_DEVFS_NAME_MAX];
    enum devfs_node_type         type;
    enum devfs_dev_class         devclass;
    u32                          mode;        /* rwx bits (octal style) */
    u32                          size;        /* file size hint */
    u32                          rdev;        /* major/minor packed */
    struct devfs_ops            *ops;
    void                        *priv;        /* driver-private data */
    struct devfs_node           *parent;
    u32                          child_count;
    u32                          children[DEVFS_MAX_CHILDREN]; /* indices into g_nodes */
    u32                          id;          /* inode-like unique id */
};

/*
 * Operations table set: groups add / remove / lookup into a single
 * struct so callers can extend devfs with custom front-ends.
 */
struct devfs_opset {
    int  (*add)(struct devfs_node *node);
    int  (*remove)(const char *path);
    struct devfs_node *(*find)(const char *path);
    int  (*open)(struct devfs_node *node);
    int  (*close)(struct devfs_node *node);
    int  (*read)(struct devfs_node *node, u32 off, void *buf, u32 len);
    int  (*write)(struct devfs_node *node, u32 off, const void *buf, u32 len);
    int  (*stat)(struct devfs_node *node, struct xkern_stat *st);
    int  (*getdents)(const char *path, u32 index, char *name, u32 namelen);
};

/* ---- core API ---- */
int                  devfs_init(void);
int                  devfs_node_add(struct devfs_node *node);
int                  devfs_node_remove(const char *path);
struct devfs_node   *devfs_node_find(const char *path);
struct devfs_node   *devfs_node_find_id(u32 id);
u32                  devfs_node_count(void);

/* helper: add a directory node */
int                  devfs_mkdir(const char *path);

/* helper: add a device node with ops */
int                  devfs_add_device(const char *path,
                                      enum devfs_node_type type,
                                      struct devfs_ops *ops,
                                      void *priv);

/* VFS integration (called from bsd fs layer) */
int                  devfs_vfs_open(const char *path, u32 flags);
int                  devfs_vfs_close(int fd);
int                  devfs_vfs_read(int fd, void *buf, u32 n);
int                  devfs_vfs_write(int fd, const void *buf, u32 n);
int                  devfs_vfs_stat(const char *path, struct xkern_stat *st);
int                  devfs_vfs_getdents(const char *path, u32 index,
                                        char *name, u32 namelen);

/* debug */
void                 devfs_dump(void);

/* the global opset (default implementation) */
extern struct devfs_opset g_devfs_opset;

#endif
