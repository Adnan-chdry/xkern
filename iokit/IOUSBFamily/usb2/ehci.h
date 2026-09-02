#ifndef EHCI_H
#define EHCI_H

/*
 * ehci - EHCI (USB2) host controller driver.
 *
 * Poll based, no interrupts, in the style of the OHCI/xHCI drivers.
 * This is the driver that owns the USB ports on boards whose firmware
 * keeps them wired to the companion EHCI controllers and fights every
 * attempt at xHCI port routing.
 */

#include "../core/usb.h"

void ehci_setup(void);
struct usb_pipe *ehci_realloc_pipe(struct usbdevice_s *usbdev,
                                    struct usb_pipe *upipe,
                                    struct usb_endpoint_descriptor *epdesc);
int ehci_send_pipe(struct usb_pipe *p, int dir, const void *cmd,
                   void *data, int datasize);
int ehci_poll_intr(struct usb_pipe *p, void *data);

#endif
