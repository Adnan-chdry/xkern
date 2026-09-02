#include "usbhid.h"
#include "../core/usb.h"
#include "atkbd.h"
#include "atmouse.h"
#include "serial.h"
#include "vga.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include <stddef.h>
#include "klibc.h"
#include "../../../bsd/devfs/devfs.h"

#define KEYREPEATWAITMS 500
#define KEYREPEATMS     33

#define HID_KBD_REPORT_SIZE 8
#define HID_MOUSE_REPORT_SIZE 4

struct pipe_node {
    struct usb_pipe *pipe;
    u8 is_mouse;
    u8 prev_report[8];
    u8 last_report[16];     /* most recent raw input report (for devfs) */
    u8 report_size;          /* generic-HID input report length */
    /* mouse accumulators (drained by usb_mouse_sample) */
    s32 dx_acc;
    s32 dy_acc;
    u8 buttons;
    struct pipe_node *next;
};

/* Ring buffer of ASCII keystrokes for the /dev/input/ukbdN char node. */
static u8 g_kbd_ring[256];
static u32 g_kbd_rp, g_kbd_wp;
static int g_ukbd_idx, g_umouse_idx, g_uhid_idx;

static void usbhid_kbd_push_char(u8 c)
{
    if (!c)
        return;
    g_kbd_ring[g_kbd_wp++ & 255] = c;
}

/* devfs read (BSD API) for a USB keyboard: drain queued keystrokes. */
static int usbhid_kbd_read(struct devfs_node *node, u32 off, void *buf,
                           u32 len)
{
    (void)node; (void)off;
    u8 *p = buf;
    u32 n = 0;
    while (n < len && g_kbd_rp != g_kbd_wp)
        p[n++] = g_kbd_ring[g_kbd_rp++ & 255];
    return n;
}

/* devfs read (BSD API) for a USB mouse / generic HID: latest report. */
static int usbhid_hid_read(struct devfs_node *node, u32 off, void *buf,
                           u32 len)
{
    (void)off;
    struct pipe_node *pn = node->priv;
    u8 sz = pn && pn->report_size ? pn->report_size : 8;
    if (len > sz)
        len = sz;
    if (pn)
        klibc.memcpy(buf, pn->last_report, len);
    return len;
}

static struct devfs_ops usbhid_kbd_ops = {
    .name = "ukbd",
    .devclass = DEVFS_CLASS_HID_KBD,
    .read = usbhid_kbd_read,
};

static struct devfs_ops usbhid_mouse_ops = {
    .name = "umouse",
    .devclass = DEVFS_CLASS_HID_MOUSE,
    .read = usbhid_hid_read,
};

static struct devfs_ops usbhid_hid_ops = {
    .name = "uhid",
    .devclass = DEVFS_CLASS_USB,
    .read = usbhid_hid_read,
};

/* Register a char devfs node for a USB HID interface under /dev/usb.
 * USB enumeration runs before the static /dev/input tree is built, so we
 * keep HID nodes in their own directory that we create on demand. */
static void usbhid_add_node(const char *prefix, int idx,
                            struct pipe_node *node, struct devfs_ops *ops)
{
    char path[48];
    static int usb_dir_made;
    if (!usb_dir_made) {
        devfs_mkdir("/dev/usb");
        usb_dir_made = 1;
    }
    klibc.snprintf(path, sizeof(path), "/dev/usb/%s%d", prefix, idx);
    devfs_add_device(path, DEVFS_NODE_CHAR, ops, node);
}

static struct pipe_node *keyboards;
static struct pipe_node *mice;
static int g_usb_kbd_present;
static int g_usb_mouse_present;

static const u8 g_hid_key_to_char[256] = {
    [0x04] = 'a', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd',
    [0x08] = 'e', [0x09] = 'f', [0x0A] = 'g', [0x0B] = 'h',
    [0x0C] = 'i', [0x0D] = 'j', [0x0E] = 'k', [0x0F] = 'l',
    [0x10] = 'm', [0x11] = 'n', [0x12] = 'o', [0x13] = 'p',
    [0x14] = 'q', [0x15] = 'r', [0x16] = 's', [0x17] = 't',
    [0x18] = 'u', [0x19] = 'v', [0x1A] = 'w', [0x1B] = 'x',
    [0x1C] = 'y', [0x1D] = 'z', [0x1E] = '1', [0x1F] = '2',
    [0x20] = '3', [0x21] = '4', [0x22] = '5', [0x23] = '6',
    [0x24] = '7', [0x25] = '8', [0x26] = '9', [0x27] = '0',
    [0x28] = '\n', [0x29] = 0, [0x2A] = '\b', [0x2B] = '\t',
    [0x2C] = ' ', [0x2D] = '-', [0x2E] = '=', [0x2F] = '[',
    [0x30] = ']', [0x31] = '\\', [0x32] = '#', [0x33] = ';',
    [0x34] = '\'', [0x35] = '`', [0x36] = ',', [0x37] = '.',
    [0x38] = '/',
};

static int set_protocol(struct usb_pipe *pipe, u16 val, u16 iface)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    req.bRequest = HID_REQ_SET_PROTOCOL;
    req.wValue = val;
    req.wIndex = iface;
    req.wLength = 0;
    return usb_send_default_control(pipe, &req, NULL);
}

static int set_idle(struct usb_pipe *pipe, int ms)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    req.bRequest = HID_REQ_SET_IDLE;
    req.wValue = (ms / 4) << 8;
    req.wIndex = 0;
    req.wLength = 0;
    return usb_send_default_control(pipe, &req, NULL);
}

/* Common boot-protocol init for keyboard and mouse interfaces */
static struct pipe_node *hid_boot_init(struct usbdevice_s *usbdev,
                                       struct usb_endpoint_descriptor *epdesc,
                                       int is_mouse)
{
    if (set_protocol(usbdev->defpipe, 0, usbdev->iface->bInterfaceNumber)) {
        klog("USBHID", "failed to set boot protocol");
        return NULL;
    }
    if (set_idle(usbdev->defpipe, KEYREPEATMS))
        klog("USBHID", "warning: failed to set idle");

    struct usb_pipe *pipe = usb_alloc_pipe(usbdev, epdesc);
    if (!pipe)
        return NULL;

    struct pipe_node *node = usb_alloc(sizeof(*node));
    if (!node)
        return NULL;
    klibc.memset(node, 0, sizeof(*node));
    node->pipe = pipe;
    node->is_mouse = is_mouse;

    if (is_mouse) {
        node->next = mice;
        mice = node;
        g_usb_mouse_present = 1;
    } else {
        node->next = keyboards;
        keyboards = node;
        g_usb_kbd_present = 1;
    }
    return node;
}

int usb_kbd_setup(struct usbdevice_s *usbdev)
{
    struct usb_endpoint_descriptor *epdesc = usb_find_desc(
        usbdev, USB_ENDPOINT_XFER_INT, USB_DIR_IN);
    if (!epdesc) {
        klog("USBHID", "no interrupt IN endpoint on device");
        return -1;
    }
    if (epdesc->wMaxPacketSize < 8 || epdesc->wMaxPacketSize > 16) {
        klog("USBHID", "keyboard maxpacket=%d unsupported",
             epdesc->wMaxPacketSize);
        return -1;
    }
    struct pipe_node *node = hid_boot_init(usbdev, epdesc, 0);
    if (!node)
        return -1;
    usbhid_add_node("ukbd", g_ukbd_idx++, node, &usbhid_kbd_ops);
    klog("USBHID", "USB keyboard initialized");
    return 0;
}

int usb_mouse_setup(struct usbdevice_s *usbdev)
{
    struct usb_endpoint_descriptor *epdesc = usb_find_desc(
        usbdev, USB_ENDPOINT_XFER_INT, USB_DIR_IN);
    if (!epdesc) {
        klog("USBHID", "no interrupt IN endpoint on device");
        return -1;
    }
    if (epdesc->wMaxPacketSize < 3
        || epdesc->wMaxPacketSize > HID_MOUSE_REPORT_SIZE + 4) {
        klog("USBHID", "mouse maxpacket=%d unsupported",
             epdesc->wMaxPacketSize);
        return -1;
    }
    struct pipe_node *node = hid_boot_init(usbdev, epdesc, 1);
    if (!node)
        return -1;
    usbhid_add_node("umouse", g_umouse_idx++, node, &usbhid_mouse_ops);
    klog("USBHID", "USB mouse initialized");
    return 0;
}

/* Legacy entry point: route a boot-HID interface to its driver */
int usb_hid_setup(struct usbdevice_s *usbdev)
{
    struct usb_interface_descriptor *iface = usbdev->iface;
    if (iface->bInterfaceSubClass != USB_INTERFACE_SUBCLASS_BOOT)
        return -1;
    if (iface->bInterfaceProtocol == USB_INTERFACE_PROTOCOL_KEYBOARD)
        return usb_kbd_setup(usbdev);
    if (iface->bInterfaceProtocol == USB_INTERFACE_PROTOCOL_MOUSE)
        return usb_mouse_setup(usbdev);
    return -1;
}

/*
 * Generic (non-boot) HID device - tablets, touchpads, gamepads, etc.
 * These expose a custom report descriptor and do not support the boot
 * protocol.  We claim the interface, switch it to report protocol, and
 * poll the interrupt-IN endpoint using its own max packet size as the
 * report length.  Movement reports are fed through the mouse path so the
 * device is at least minimally usable; this never blocks enumeration.
 */
int usb_hid_generic_setup(struct usbdevice_s *usbdev)
{
    struct usb_endpoint_descriptor *epdesc = usb_find_desc(
        usbdev, USB_ENDPOINT_XFER_INT, USB_DIR_IN);
    if (!epdesc) {
        klog("USBHID", "no interrupt IN endpoint on HID device");
        return -1;
    }

    set_idle(usbdev->defpipe, KEYREPEATMS);
    /* report protocol (1); boot (0) is unsupported on generic HID */
    if (set_protocol(usbdev->defpipe, 1, usbdev->iface->bInterfaceNumber))
        klog("USBHID", "warning: generic HID set_protocol failed");

    int sz = epdesc->wMaxPacketSize;
    if (sz < 1 || sz > 64)
        sz = 8;

    struct usb_pipe *pipe = usb_alloc_pipe(usbdev, epdesc);
    if (!pipe)
        return -1;

    struct pipe_node *node = usb_alloc(sizeof(*node));
    if (!node) {
        usb_free_pipe(usbdev, pipe);
        return -1;
    }
    klibc.memset(node, 0, sizeof(*node));
    node->pipe = pipe;
    node->is_mouse = 1;            /* feed through mouse accumulator */
    node->report_size = sz;

    node->next = mice;
    mice = node;
    g_usb_mouse_present = 1;

    usbhid_add_node("uhid", g_uhid_idx++, node, &usbhid_hid_ops);
    klog("USBHID", "USB HID device initialized (report size %d)", sz);
    return 0;
}

int usb_kbd_available(void)
{
    return g_usb_kbd_present;
}

int usb_mouse_present(void)
{
    return g_usb_mouse_present;
}

/****************************************************************
 * Report parsing
 ****************************************************************/
static int key_in_report(const u8 *report, u8 key)
{
    for (int i = 2; i < HID_KBD_REPORT_SIZE; i++)
        if (report[i] == key)
            return 1;
    return 0;
}

static void parse_keyboard_report(struct pipe_node *node, u8 *report)
{
    u8 modifiers = report[0];
    for (int i = 2; i < HID_KBD_REPORT_SIZE; i++) {
        u8 key = report[i];
        if (!key)
            continue;
        if (key_in_report(node->prev_report, key))
            continue;
        u8 c = g_hid_key_to_char[key];
        if (!c)
            continue;
        if (modifiers & 0x22)
            c -= ('a' - 'A');
        if (c == '\b')
            vga_backspace();
        else
            vga_putchar(c);
        kbd_push(c);
        usbhid_kbd_push_char(c);
    }
    klibc.memcpy(node->prev_report, report, HID_KBD_REPORT_SIZE);
}

/* Boot protocol mouse report: buttons, dx, dy [, wheel] */
static void parse_mouse_report(struct pipe_node *node, const u8 *report)
{
    s8 dx = (s8)report[1];
    s8 dy = (s8)report[2];
    node->dx_acc += dx;
    node->dy_acc += dy;         /* boot reports are +y down, like PS/2 */
    node->buttons = report[0] & 0x07;
    klibc.memcpy(node->last_report, report, sizeof(node->last_report));
}

static void poll_nodes(struct pipe_node *head)
{
    for (struct pipe_node *node = head; node; node = node->next) {
        for (;;) {
            u8 data[16];
            int ret = usb_poll_intr(node->pipe, data);
            if (ret)
                break;
            if (node->is_mouse)
                parse_mouse_report(node, data);
            else
                parse_keyboard_report(node, data);
        }
    }
}

void usb_check_event(void)
{
    u64 flags;
    asm volatile ("pushfq; popq %0; cli" : "=r"(flags));

    poll_nodes(keyboards);
    poll_nodes(mice);

    if (flags & 0x200)
        asm volatile ("sti");
}

int usb_mouse_sample(int *dx, int *dy, u8 *buttons)
{
    if (!g_usb_mouse_present)
        return 0;

    int adx = 0, ady = 0;
    u8 btn = 0;
    for (struct pipe_node *node = mice; node; node = node->next) {
        adx += node->dx_acc;
        ady += node->dy_acc;
        node->dx_acc = 0;
        node->dy_acc = 0;
        btn |= node->buttons;
    }
    if (buttons)
        *buttons = btn;
    if (dx)
        *dx += adx;
    if (dy)
        *dy += ady;
    return 1;
}

int usb_getchar(void)
{
    usb_check_event();
    if (!usb_kbd_available())
        atkbd_poll();
    int c = atkbd_getchar();
    if (c >= 0)
        return 0;
    return serial_getchar();
}