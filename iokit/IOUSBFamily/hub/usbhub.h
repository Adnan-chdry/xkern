#ifndef USBHUB_H
#define USBHUB_H

#include "../core/usb.h"

#define USB_DT_HUB   0x29
#define USB_DT_HUB3  0x2A

#define USB_PORT_FEAT_CONNECTION 0
#define USB_PORT_FEAT_ENABLE     1
#define USB_PORT_FEAT_SUSPEND    2
#define USB_PORT_FEAT_RESET      4
#define USB_PORT_FEAT_POWER      8

#define USB_PORT_STAT_CONNECTION 0x0001
#define USB_PORT_STAT_ENABLE     0x0002
#define USB_PORT_STAT_SUSPEND    0x0004
#define USB_PORT_STAT_RESET      0x0010
#define USB_PORT_STAT_POWER      0x0100
#define USB_PORT_STAT_LOW_SPEED  0x0200
#define USB_PORT_STAT_HIGH_SPEED 0x0400
#define USB_PORT_STAT_SPEED_SHIFT 9
#define USB_PORT_STAT_SPEED_MASK (0x3 << USB_PORT_STAT_SPEED_SHIFT)

struct usb_hub_descriptor {
    u8  bDescLength;
    u8  bDescriptorType;
    u8  bNbrPorts;
    u16 wHubCharacteristics;
    u8  bPwrOn2PwrGood;
    u8  bHubContrCurrent;
} __attribute__((packed));

struct usb_port_status {
    u16 wPortStatus;
    u16 wPortChange;
} __attribute__((packed));

int usb_hub_setup(struct usbdevice_s *usbdev);

#endif
