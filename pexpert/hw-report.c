/*
 *  hw-report.c - Hardware report & devfs node scanner.
 *
 *  Scans all discovered subsystems after boot and creates the
 *  correct /dev nodes with struct devfs_ops attached.
 *
 *  Tree produced:
 *      /dev/cpu0, cpu1, ...
 *      /dev/disk0, disk0p1, ...  /dev/loop0, ...  /dev/nvme0n1, ...
 *      /dev/kbd0  /dev/mouse0
 *      /dev/fb0
 *      /dev/audio0, audio1, ...
 *      /dev/ttyS0, ttyS1, ...
 *      /dev/pci0, pci1, ...
 *      /dev/net0, ...
 */

#include "IOAudioFamily/hda.h"
#include "IOGraphicsFamily/fb.h"
#include "IOHIDFamily/atkbd.h"
#include "IOHIDFamily/atmouse.h"
#include "IOPCIFamily/pci.h"
#include "IOStorageFamily/devfs/devfs.h"
#include "IOStorageFamily/io_storage.h"
#include "cpu.h"
#include "smp.h"
#include "devfs.h"
#include "io.h"
#include "klibc.h"
#include "klog.h"

/* ------------------------------------------------------------------ */
/*  Per-class ops stubs (most are read-through to the low-level devfs) */
/* ------------------------------------------------------------------ */

static int
cpu_open (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
cpu_close (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
cpu_read (struct devfs_node *node, u32 off, void *buf, u32 len)
{
  (void) node;
  (void) off;
  (void) buf;
  (void) len;
  return 0;
}

static struct devfs_ops cpu_ops = {
  .name = "cpu",
  .devclass = DEVFS_CLASS_CPU,
  .open = cpu_open,
  .close = cpu_close,
  .read = cpu_read,
};

/* ---- storage ---- */

static int
stor_open (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
stor_close (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
stor_read (struct devfs_node *node, u32 off, void *buf, u32 len)
{
  struct devfs_device *d;
  u32 lba = off / 512;
  u8 cnt = (u8) ((len + 511) / 512);

  (void) off;
  d = devfs_find (node->name);
  if (!d || !d->read)
    return -1;
  return d->read (d, lba, cnt, buf);
}

static int
stor_write (struct devfs_node *node, u32 off, const void *buf, u32 len)
{
  struct devfs_device *d;
  u32 lba = off / 512;
  u8 cnt = (u8) ((len + 511) / 512);

  (void) off;
  d = devfs_find (node->name);
  if (!d || !d->write)
    return -1;
  return d->write (d, lba, cnt, (void *) buf);
}

static struct devfs_ops stor_ops = {
  .name = "storage",
  .devclass = DEVFS_CLASS_STORAGE,
  .open = stor_open,
  .close = stor_close,
  .read = stor_read,
  .write = stor_write,
};

/* ---- HID keyboard ---- */

static int
kbd_open (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
kbd_close (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
kbd_read (struct devfs_node *node, u32 off, void *buf, u32 len)
{
  u8 *out = (u8 *) buf;
  u32 i;

  (void) node;
  (void) off;
  for (i = 0; i < len; i++)
    out[i] = (u8) atkbd_getchar ();
  return (int) len;
}

static struct devfs_ops kbd_ops = {
  .name = "kbd",
  .devclass = DEVFS_CLASS_HID_KBD,
  .open = kbd_open,
  .close = kbd_close,
  .read = kbd_read,
};

/* ---- HID mouse ---- */

static int
mouse_open (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
mouse_close (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
mouse_read (struct devfs_node *node, u32 off, void *buf, u32 len)
{
  (void) node;
  (void) off;
  if (len >= 3)
    {
      u8 *out = (u8 *) buf;
      int dx, dy;
      u8 buttons;

      if (atmouse_sample (&dx, &dy, &buttons))
        {
          out[0] = (u8) dx;
          out[1] = (u8) dy;
          out[2] = buttons;
          return 3;
        }
    }
  return 0;
}

static struct devfs_ops mouse_ops = {
  .name = "mouse",
  .devclass = DEVFS_CLASS_HID_MOUSE,
  .open = mouse_open,
  .close = mouse_close,
  .read = mouse_read,
};

/* ---- framebuffer ---- */

static int
fb_open (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
fb_close (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
fb_read (struct devfs_node *node, u32 off, void *buf, u32 len)
{
  u32 fb_sz;

  (void) node;
  (void) buf;
  if (!framebuffer_ready ())
    return -1;
  fb_sz = framebuffer_width () * framebuffer_height () * 4;
  if (off >= fb_sz)
    return 0;
  if (off + len > fb_sz)
    len = fb_sz - off;
  /* fb memory mapped at framebuffer address; userspace reads via mmap */
  return (int) len;
}

static struct devfs_ops fb_ops = {
  .name = "framebuffer",
  .devclass = DEVFS_CLASS_FRAMEBUFFER,
  .open = fb_open,
  .close = fb_close,
  .read = fb_read,
};

/* ---- audio (HDA) ---- */

static int
audio_open (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
audio_close (struct devfs_node *node)
{
  (void) node;
  return 0;
}

static struct devfs_ops audio_ops = {
  .name = "audio",
  .devclass = DEVFS_CLASS_AUDIO,
  .open = audio_open,
  .close = audio_close,
};

/* ---- serial ---- */

static int
serial_open (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
serial_close (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
serial_read (struct devfs_node *node, u32 off, void *buf, u32 len)
{
  u8 *out = (u8 *) buf;
  u32 i;
  u16 port = 0x3F8;

  (void) node;
  (void) off;
  for (i = 0; i < len; i++)
    {
      while (!(inb (port + 5) & 0x01))
        ;
      out[i] = inb (port);
    }
  return (int) len;
}

static int
serial_write (struct devfs_node *node, u32 off, const void *buf, u32 len)
{
  const u8 *p = (const u8 *) buf;
  u32 i;
  u16 port = 0x3F8;

  (void) node;
  (void) off;
  for (i = 0; i < len; i++)
    {
      while (!(inb (port + 5) & 0x20))
        ;
      outb (port, p[i]);
    }
  return (int) len;
}

static struct devfs_ops serial_ops = {
  .name = "serial",
  .devclass = DEVFS_CLASS_SERIAL,
  .open = serial_open,
  .close = serial_close,
  .read = serial_read,
  .write = serial_write,
};

/*USB*/
static int usb_open(struct devfs_node *node){
  (void) node;
  return 0;
}
static int usb_close(struct devfs_node *node){
  (void) node;
  return 0;
}
static struct devfs_ops usb_ops ={
  .name = "usb",
  .devclass = DEVFS_CLASS_USB,
  .open = usb_open,
  .close = usb_close,
};

/* ---- PCI generic ---- */

static int
pci_open (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
pci_close (struct devfs_node *node)
{
  (void) node;
  return 0;
}

static struct devfs_ops pci_ops = {
  .name = "pci",
  .devclass = DEVFS_CLASS_PCI,
  .open = pci_open,
  .close = pci_close,
};

/* ---- network ---- */

static int
net_open (struct devfs_node *node)
{
  (void) node;
  return 0;
}
static int
net_close (struct devfs_node *node)
{
  (void) node;
  return 0;
}

static struct devfs_ops net_ops = {
  .name = "network",
  .devclass = DEVFS_CLASS_NETWORK,
  .open = net_open,
  .close = net_close,
};

/* ================================================================ */
/*  Scanner functions                                               */
/* ================================================================ */

static void
scan_cpu (void)
{
  u32 count = smp_get_cpu_count ();
  u32 i;

  if (count == 0)
    count = 1;

  for (i = 0; i < count && i < 8; i++)
    {
      char path[DEVFS_PATH_MAX];

      klibc.snprintf (path, sizeof (path), "/dev/cpu/cpu%u", i);
      devfs_add_device (path, DEVFS_NODE_CHAR, &cpu_ops, 0);
    }

  klog ("hw.report", "cpu: %u processor(s) detected", count);
}

static void
scan_storage (void)
{
  int count;
  int i;

  count = devfs_count ();
  for (i = 0; i < count; i++)
    {
      struct devfs_device *d = devfs_get (i);
      char path[DEVFS_PATH_MAX];

      if (!d)
        continue;
      klibc.snprintf (path, sizeof (path), "/dev/disk/%s", d->name);
      devfs_add_device (path, DEVFS_NODE_BLOCK, &stor_ops, d);
    }
}

static void
scan_hid (void)
{
  /* PS/2 keyboard */
  devfs_add_device ("/dev/input/kbd0", DEVFS_NODE_CHAR, &kbd_ops, 0);

  /* PS/2 mouse (only if detected) */
  if (atmouse_ready ())
    devfs_add_device ("/dev/input/mouse0", DEVFS_NODE_CHAR, &mouse_ops, 0);
}

static void
scan_framebuffer (void)
{
  if (framebuffer_ready ())
    {
      struct devfs_node node;

      klibc.memset (&node, 0, sizeof (node));
      klibc.strncpy (node.name, "/dev/fb/fb0", BSD_DEVFS_NAME_MAX - 1);
      node.type = DEVFS_NODE_CHAR;
      node.devclass = DEVFS_CLASS_FRAMEBUFFER;
      node.mode = 0660;
      node.size = framebuffer_width () * framebuffer_height () * 4;
      node.ops = &fb_ops;
      devfs_node_add (&node);
    }
}

static void
scan_audio (void)
{
  /* HDA audio: create /dev/audio/audio0 for each codec found */
  /* The HDA controller scan already happened in io_storage_init() */
  devfs_add_device ("/dev/audio/audio0", DEVFS_NODE_CHAR, &audio_ops, 0);
}

static void
scan_serial (void)
{
  /* Standard COM1-COM4 */
  static const u16 com_ports[] = { 0x3F8, 0x2F8, 0x3E8, 0x2E8 };
  u32 i;

  for (i = 0; i < 4; i++)
    {
      char path[DEVFS_PATH_MAX];

      klibc.snprintf (path, sizeof (path), "/dev/serial/ttyS%u", i);
      devfs_add_device (path, DEVFS_NODE_CHAR, &serial_ops,
                        (void *) (uptr) com_ports[i]);
    }
}

static void
scan_pci (void)
{
  int count;
  int i;

  count = pci_device_count ();
  for (i = 0; i < count; i++)
    {
      struct pci_device *d = pci_get_device (i);
      char path[DEVFS_PATH_MAX];

      if (!d)
        continue;
      klibc.snprintf (path, sizeof (path), "/dev/pci/pci%x:%x.%x",
                      d->bus, d->dev, d->func);
      devfs_add_device (path, DEVFS_NODE_CHAR, &pci_ops, d);
    }
}

static void
scan_network (void)
{
  /* placeholder: ionet would register /dev/net/netN here */
  devfs_add_device ("/dev/net/net0", DEVFS_NODE_CHAR, &net_ops, 0);
}

/* ================================================================ */
/*  Public entry point                                              */
/* ================================================================ */

void
hw_report_scan (void)
{
  klog ("hw.report", "hw_report_scan() start");

  /* 1. init the BSD devfs (creates /dev root) */
  devfs_init ();

  /* 2. create subdirectory structure */
  devfs_mkdir ("/dev/cpu");
  devfs_mkdir ("/dev/disk");
  devfs_mkdir ("/dev/input");
  devfs_mkdir ("/dev/fb");
  devfs_mkdir ("/dev/audio");
  devfs_mkdir ("/dev/net");
  devfs_mkdir ("/dev/pci");
  devfs_mkdir ("/dev/serial");

  /* 3. scan each subsystem and populate nodes */
  scan_cpu ();
  scan_storage ();
  scan_hid ();
  scan_framebuffer ();
  scan_audio ();
  scan_serial ();
  scan_pci ();
  scan_network ();

  /* 4. dump the final tree */
  devfs_dump ();

  klog ("hw.report", "hw_report_scan() done");
}

