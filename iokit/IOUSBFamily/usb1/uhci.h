#ifndef UHCI_H
#define UHCI_H

/*
 * uhci - UHCI (USB1) host controller driver.
 *
 * Register-level algorithms and data structures derived from the Linux
 * kernel UHCI host driver (linux/drivers/usb/host/uhci-*.c, GPL-2.0).
 * Adapted to xkern's compact, poll-based, freestanding driver model.
 */

#include "../core/usb.h"

void uhci_setup(void);
struct usb_pipe *uhci_realloc_pipe(struct usbdevice_s *usbdev,
                                   struct usb_pipe *upipe,
                                   struct usb_endpoint_descriptor *epdesc);
int uhci_send_pipe(struct usb_pipe *p, int dir, const void *cmd,
                   void *data, int datasize);
int uhci_poll_intr(struct usb_pipe *p, void *data);

#endif
