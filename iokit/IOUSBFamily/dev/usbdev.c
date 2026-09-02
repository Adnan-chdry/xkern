#include "usbdev.h"
#include "../hub/usbhub.h"
#include "../hid/usbhid.h"
#include "../msc/usbmsc.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include "klibc.h"

#define USBDEV_MAX 16

static struct usbdev_entry g_entries[USBDEV_MAX];
static int g_count;

static const char *type_name(enum usbdev_type t)
{
    switch (t) {
    case USBDEV_HUB:          return "hub";
    case USBDEV_KEYBOARD:     return "keyboard";
    case USBDEV_MOUSE:        return "mouse";
    case USBDEV_MASS_STORAGE: return "mass-storage";
    case USBDEV_HID:           return "hid";
    default:                  return "vendor-specific";
    }
}

static const char *bus_name(u8 bus)
{
    switch (bus) {
    case USB_TYPE_XHCI: return "xhci";
    case USB_TYPE_OHCI: return "ohci";
    default:            return "usb";
    }
}

int usbdev_classify(const struct usb_interface_descriptor *iface)
{
    if (iface->bInterfaceClass == USB_CLASS_HUB)
        return USBDEV_HUB;

    if (iface->bInterfaceClass == USB_CLASS_HID) {
        if (iface->bInterfaceSubClass == USB_INTERFACE_SUBCLASS_BOOT) {
            if (iface->bInterfaceProtocol == USB_INTERFACE_PROTOCOL_KEYBOARD)
                return USBDEV_KEYBOARD;
            if (iface->bInterfaceProtocol == USB_INTERFACE_PROTOCOL_MOUSE)
                return USBDEV_MOUSE;
        }
        /* generic (non-boot) HID: tablets, touchpads, gamepads, ... */
        return USBDEV_HID;
    }

    if (iface->bInterfaceClass == USB_CLASS_MASS_STORAGE) {
        if (iface->bInterfaceProtocol == US_PR_BULK)
            return USBDEV_MASS_STORAGE;
        /* UAS and vendor protocols unsupported */
        return USBDEV_VENDOR;
    }

    return USBDEV_VENDOR;
}

int usbdev_attach(struct usbdevice_s *usbdev)
{
    int type = usbdev_classify(usbdev->iface);

    switch (type) {
    case USBDEV_HUB:
        if (usb_hub_setup(usbdev))
            return -1;
        break;
    case USBDEV_KEYBOARD:
        if (usb_kbd_setup(usbdev))
            return -1;
        break;
    case USBDEV_MOUSE:
        if (usb_mouse_setup(usbdev))
            return -1;
        break;
    case USBDEV_MASS_STORAGE:
        if (usb_msc_setup(usbdev))
            return -1;
        break;
    case USBDEV_HID:
        if (usb_hid_generic_setup(usbdev))
            return -1;
        break;
    default:
        klog("USBDevice",
             "unclaimed iface %d: cls=%02x sub=%02x proto=%02x",
             usbdev->iface->bInterfaceNumber, usbdev->iface->bInterfaceClass,
             usbdev->iface->bInterfaceSubClass,
             usbdev->iface->bInterfaceProtocol);
        return -1;
    }

    return type;
}

/* Best-effort product string read.  Never fails enumeration. */
static void usbdev_read_string(struct usb_pipe *pipe, u8 idx,
                               char *out, u32 outsz)
{
    out[0] = 0;
    if (!idx || outsz < 2)
        return;

    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_GET_DESCRIPTOR;
    req.wValue = (USB_DT_STRING << 8) | idx;
    req.wIndex = 0;             /* langid */
    req.wLength = 64;

    u8 buf[64];
    klibc.memset(buf, 0, sizeof(buf));
    if (usb_send_default_control(pipe, &req, buf))
        return;
    if (buf[0] < 4 || buf[1] != USB_DT_STRING)
        return;

    u32 n = (buf[0] - 2) / 2;
    if (n > outsz - 1)
        n = outsz - 1;
    for (u32 i = 0; i < n; i++) {
        u8 c = buf[2 + i * 2];
        out[i] = (c >= 0x20 && c < 0x7f) ? c : '?';
    }
    out[n] = 0;
}

int usbdev_register(struct usbdevice_s *usbdev, enum usbdev_type type,
                    u16 vid, u16 pid, u8 iProduct)
{
    if (g_count >= USBDEV_MAX)
        return -1;

    struct usbdev_entry *e = &g_entries[g_count];
    klibc.memset(e, 0, sizeof(*e));
    e->type = type;
    e->bus = usbdev->hub->cntl->type;
    e->addr = usbdev->devaddr;
    e->port = usbdev->port;
    e->speed = usbdev->speed;
    e->iface = usbdev->iface ? usbdev->iface->bInterfaceNumber : 0;
    e->vid = vid;
    e->pid = pid;
    usbdev_read_string(usbdev->defpipe, iProduct, e->product,
                       sizeof(e->product));

    klog("USBDevice", "[%s] port %d addr %d: %s vid=%04x pid=%04x%s%s%s",
         bus_name(e->bus), e->port + 1, e->addr, type_name(type),
         e->vid, e->pid,
         e->product[0] ? " \"" : "",
         e->product[0] ? e->product : "",
         e->product[0] ? "\"" : "");

    g_count++;
    return g_count - 1;
}

int usbdev_count(void)
{
    return g_count;
}

const struct usbdev_entry *usbdev_get(int i)
{
    if (i < 0 || i >= g_count)
        return NULL;
    return &g_entries[i];
}

void usbdev_dump(void)
{
    klog("USBDevice", "%d device(s):", g_count);
    for (int i = 0; i < g_count; i++) {
        struct usbdev_entry *e = &g_entries[i];
        klog("USBDevice", "  %s%d: [%s] port %d addr %d %s vid=%04x pid=%04x",
             bus_name(e->bus), i, type_name(e->type), e->port + 1,
             e->addr, e->product[0] ? e->product : "", e->vid, e->pid);
    }
}
