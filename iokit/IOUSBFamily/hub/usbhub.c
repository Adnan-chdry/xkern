#include "usbhub.h"
#include "../core/usb.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include <stddef.h>
#include "klibc.h"

static int get_hub_desc(struct usb_pipe *pipe, struct usb_hub_descriptor *desc)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_GET_DESCRIPTOR;
    req.wValue = USB_DT_HUB << 8;
    req.wIndex = 0;
    req.wLength = sizeof(*desc);
    return usb_send_default_control(pipe, &req, desc);
}

static int set_port_feature(struct usbhub_s *hub, int port, int feature)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER;
    req.bRequest = USB_REQ_SET_FEATURE;
    req.wValue = feature;
    req.wIndex = port + 1;
    req.wLength = 0;
    return usb_send_default_control(hub->usbdev->defpipe, &req, NULL);
}

static int clear_port_feature(struct usbhub_s *hub, int port, int feature)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER;
    req.bRequest = USB_REQ_CLEAR_FEATURE;
    req.wValue = feature;
    req.wIndex = port + 1;
    req.wLength = 0;
    return usb_send_default_control(hub->usbdev->defpipe, &req, NULL);
}

static int get_port_status(struct usbhub_s *hub, int port,
                           struct usb_port_status *sts)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_OTHER;
    req.bRequest = USB_REQ_GET_STATUS;
    req.wValue = 0;
    req.wIndex = port + 1;
    req.wLength = sizeof(*sts);
    return usb_send_default_control(hub->usbdev->defpipe, &req, sts);
}

static int usb_hub_detect(struct usbhub_s *hub, u32 port)
{
    struct usb_port_status sts;
    int ret = get_port_status(hub, port, &sts);
    if (ret) {
        klog("USBHUB", "port %d detect failed", port);
        return -1;
    }
    return (sts.wPortStatus & USB_PORT_STAT_CONNECTION) ? 1 : 0;
}

static void usb_hub_disconnect(struct usbhub_s *hub, u32 port)
{
    int ret = clear_port_feature(hub, port, USB_PORT_FEAT_ENABLE);
    if (ret)
        klog("USBHUB", "port %d disconnect failed", port);
}

static int usb_hub_reset(struct usbhub_s *hub, u32 port)
{
    int ret = set_port_feature(hub, port, USB_PORT_FEAT_RESET);
    if (ret)
        goto fail;

    struct usb_port_status sts;
    u32 end = usb_now_ms() + USB_TIME_DRST * 2;
    for (;;) {
        ret = get_port_status(hub, port, &sts);
        if (ret)
            goto fail;
        if (!(sts.wPortStatus & USB_PORT_STAT_RESET))
            break;
        if (usb_now_ms() >= end)
            goto fail;
        usb_msleep(5);
    }

    if (!(sts.wPortStatus & USB_PORT_STAT_CONNECTION))
        return -1;

    return (sts.wPortStatus & USB_PORT_STAT_SPEED_MASK)
           >> USB_PORT_STAT_SPEED_SHIFT;

fail:
    klog("USBHUB", "port %d reset failed", port);
    usb_hub_disconnect(hub, port);
    return -1;
}

static struct usbhub_op_s HubOp = {
    .detect = usb_hub_detect,
    .reset = usb_hub_reset,
    .disconnect = usb_hub_disconnect,
};

int usb_hub_setup(struct usbdevice_s *usbdev)
{
    struct usb_hub_descriptor desc;
    int ret = get_hub_desc(usbdev->defpipe, &desc);
    if (ret)
        return ret;

    struct usbhub_s hub;
    klibc.memset(&hub, 0, sizeof(hub));
    hub.usbdev = usbdev;
    hub.cntl = usbdev->defpipe->cntl;
    hub.portcount = desc.bNbrPorts;
    hub.op = &HubOp;

    for (int port = 0; port < desc.bNbrPorts; port++) {
        ret = set_port_feature(&hub, port, USB_PORT_FEAT_POWER);
        if (ret)
            return ret;
    }
    usb_msleep(desc.bPwrOn2PwrGood * 2);

    usb_enumerate(&hub);

    klog("USBHUB", "hub %d ports, %d devices", desc.bNbrPorts, hub.devcount);
    if (hub.devcount)
        return 0;
    return -1;
}
