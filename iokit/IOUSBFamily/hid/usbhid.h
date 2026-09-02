#ifndef USBHID_H
#define USBHID_H

#include "../core/usb.h"

#define USB_INTERFACE_SUBCLASS_BOOT     1
#define USB_INTERFACE_PROTOCOL_KEYBOARD 1
#define USB_INTERFACE_PROTOCOL_MOUSE    2

#define HID_REQ_SET_PROTOCOL 0x0B
#define HID_REQ_SET_IDLE     0x0A

struct usbdevice_s;

/* class driver entry points (one per interface) */
int usb_hid_setup(struct usbdevice_s *usbdev);
int usb_kbd_setup(struct usbdevice_s *usbdev);
int usb_mouse_setup(struct usbdevice_s *usbdev);
int usb_hid_generic_setup(struct usbdevice_s *usbdev);

/* queries / polling */
int usb_kbd_available(void);
int usb_mouse_present(void);
void usb_check_event(void);
int usb_getchar(void);

/*
 * Drain accumulated USB mouse motion + button state since last call.
 * Same semantics as atmouse_sample(): dx/dy in screen space
 * (+x right, +y down), buttons bit0 left, bit1 right, bit2 middle.
 * Returns 1 when a USB mouse is present.
 */
int usb_mouse_sample(int *dx, int *dy, u8 *buttons);

#endif
