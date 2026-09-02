#ifndef USBMSC_H
#define USBMSC_H

#include "../core/usb.h"

/*
 * usbmsc - USB mass storage (Bulk-Only Transport / BOT) driver.
 *
 * Attaches to interfaces with class=0x08, subclass=0x06 (SCSI),
 * protocol=0x50 (BOT).  Wraps SCSI commands in CBW/CSW pairs over
 * bulk endpoints and registers each LUN as a devfs block device.
 */

int usb_msc_setup(struct usbdevice_s *usbdev);

#endif
