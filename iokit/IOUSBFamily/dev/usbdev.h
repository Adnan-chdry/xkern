#ifndef USBDEV_H
#define USBDEV_H

#include "../core/usb.h"

/*
 * usbdev - USB device subsystem.
 *
 * Classifies enumerated interfaces, keeps a registry of every device
 * found on the bus (keyboard / mouse / mass storage / hub / vendor),
 * and dispatches each interface to its class driver.  Works per
 * interface so composite devices (kbd+mouse combos) enumerate fully
 * on real hardware.
 */

enum usbdev_type {
    USBDEV_NONE = 0,
    USBDEV_HUB,
    USBDEV_KEYBOARD,
    USBDEV_MOUSE,
    USBDEV_MASS_STORAGE,
    USBDEV_HID,
    USBDEV_VENDOR,
};

struct usbdev_entry {
    enum usbdev_type type;
    u8  bus;        /* USB_TYPE_XHCI / USB_TYPE_OHCI */
    u8  addr;       /* device address */
    u8  port;       /* root hub port */
    u8  speed;      /* USB_xSPEED */
    u8  iface;      /* bInterfaceNumber */
    u16 vid;
    u16 pid;
    char product[32];
};

/* classify an interface descriptor */
int usbdev_classify(const struct usb_interface_descriptor *iface);

/* attach a configured interface to its class driver (0 = claimed) */
int usbdev_attach(struct usbdevice_s *usbdev);

/* register an enumerated device in the registry and log it */
int usbdev_register(struct usbdevice_s *usbdev, enum usbdev_type type,
                    u16 vid, u16 pid, u8 iProduct);

/* registry queries */
int usbdev_count(void);
const struct usbdev_entry *usbdev_get(int i);
void usbdev_dump(void);

#endif
