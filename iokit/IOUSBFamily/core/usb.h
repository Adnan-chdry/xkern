#ifndef USB_CORE_H
#define USB_CORE_H

#include "types.h"
#include <stddef.h>
#include "usb_dma.h"

#define USB_TYPE_UHCI  1
#define USB_TYPE_OHCI  2
#define USB_TYPE_EHCI  3
#define USB_TYPE_XHCI  4

#define USB_FULLSPEED  0
#define USB_LOWSPEED   1
#define USB_HIGHSPEED  2
#define USB_SUPERSPEED 3

#define USB_MAXADDR    127

/* USB mandated timings (in ms) */
#define USB_TIME_SIGATT 200
#define USB_TIME_ATTDB  100
#define USB_TIME_DRST   10
#define USB_TIME_DRSTR  50
#define USB_TIME_RSTRCY 10

#define USB_TIME_STATUS  50
#define USB_TIME_DATAIN  500
#define USB_TIME_COMMAND 5000

#define USB_TIME_SETADDR_RECOVERY 2

#define USB_DIR_OUT     0
#define USB_DIR_IN      0x80

#define USB_TYPE_MASK   (0x03 << 5)
#define USB_TYPE_STANDARD (0x00 << 5)
#define USB_TYPE_CLASS  (0x01 << 5)
#define USB_TYPE_VENDOR (0x02 << 5)

#define USB_RECIP_MASK  0x1f
#define USB_RECIP_DEVICE    0x00
#define USB_RECIP_INTERFACE 0x01
#define USB_RECIP_ENDPOINT  0x02
#define USB_RECIP_OTHER     0x03

#define USB_REQ_GET_STATUS     0x00
#define USB_REQ_CLEAR_FEATURE  0x01
#define USB_REQ_SET_FEATURE    0x03
#define USB_REQ_SET_ADDRESS    0x05
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_DESCRIPTOR 0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE  0x0A
#define USB_REQ_SET_INTERFACE  0x0B
#define USB_REQ_SYNCH_FRAME    0x0C

#define USB_DT_DEVICE   0x01
#define USB_DT_CONFIG   0x02
#define USB_DT_STRING   0x03
#define USB_DT_INTERFACE 0x04
#define USB_DT_ENDPOINT 0x05

#define USB_CLASS_HID         3
#define USB_CLASS_MASS_STORAGE 8
#define USB_CLASS_HUB         9

#define USB_ENDPOINT_NUMBER_MASK   0x0f
#define USB_ENDPOINT_DIR_MASK      0x80
#define USB_ENDPOINT_XFERTYPE_MASK 0x03
#define USB_ENDPOINT_XFER_CONTROL 0
#define USB_ENDPOINT_XFER_ISOC    1
#define USB_ENDPOINT_XFER_BULK    2
#define USB_ENDPOINT_XFER_INT     3

#define USB_CONTROL_SETUP_SIZE 8

/* USB mass storage flags */
#define US_PR_BULK 0x50
#define US_PR_UAS  0x62

struct usb_pipe;
struct usbdevice_s;
struct usbhub_s;
struct usb_s;

struct usbhub_op_s {
    int (*detect)(struct usbhub_s *hub, u32 port);
    int (*reset)(struct usbhub_s *hub, u32 port);
    void (*disconnect)(struct usbhub_s *hub, u32 port);
};

struct usb_s {
    struct usb_pipe *freelist;
    void *priv;
    u8 type;
    u8 maxaddr;
};

struct usb_pipe {
    union {
        struct usb_s *cntl;
        struct usb_pipe *freenext;
    };
    u8 type;
    u8 ep;
    u8 devaddr;
    u8 speed;
    u16 maxpacket;
    u8 eptype;
};

struct usbhub_s {
    struct usbhub_op_s *op;
    struct usbdevice_s *usbdev;
    struct usb_s *cntl;
    u32 portcount;
    u32 port;
    u32 devcount;
    u32 detectend;
    u32 threads;
};

struct usbdevice_s {
    struct usbhub_s *hub;
    struct usb_pipe *defpipe;
    u32 port;
    struct usb_config_descriptor *config;
    struct usb_interface_descriptor *iface;
    int imax;
    u8 speed;
    u8 devaddr;
};

struct usb_ctrlrequest {
    u8 bRequestType;
    u8 bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
} __attribute__((packed));

struct usb_device_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u16 bcdUSB;
    u8  bDeviceClass;
    u8  bDeviceSubClass;
    u8  bDeviceProtocol;
    u8  bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8  iManufacturer;
    u8  iProduct;
    u8  iSerialNumber;
    u8  bNumConfigurations;
} __attribute__((packed));

struct usb_config_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u16 wTotalLength;
    u8  bNumInterfaces;
    u8  bConfigurationValue;
    u8  iConfiguration;
    u8  bmAttributes;
    u8  bMaxPower;
} __attribute__((packed));

struct usb_interface_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bInterfaceNumber;
    u8  bAlternateSetting;
    u8  bNumEndpoints;
    u8  bInterfaceClass;
    u8  bInterfaceSubClass;
    u8  bInterfaceProtocol;
    u8  iInterface;
} __attribute__((packed));

struct usb_endpoint_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bEndpointAddress;
    u8  bmAttributes;
    u16 wMaxPacketSize;
    u8  bInterval;
} __attribute__((packed));

/* Time helpers (TSC based - work with interrupts disabled) */
u32 usb_now_ms(void);
void usb_msleep(int ms);

/* Pipe management */
struct usb_pipe *usb_alloc_pipe(struct usbdevice_s *usbdev,
                                struct usb_endpoint_descriptor *epdesc);
void usb_free_pipe(struct usbdevice_s *usbdev, struct usb_pipe *pipe);
int usb_send_default_control(struct usb_pipe *pipe,
                             const struct usb_ctrlrequest *req, void *data);
int usb_poll_intr(struct usb_pipe *pipe, void *data);
int usb_bulk_transfer(struct usb_pipe *pipe, int dir, void *data,
                      int datalen);
void usb_add_freelist(struct usb_pipe *pipe);
struct usb_pipe *usb_get_freelist(struct usb_s *cntl, u8 eptype);
void usb_desc2pipe(struct usb_pipe *pipe, struct usbdevice_s *usbdev,
                   struct usb_endpoint_descriptor *epdesc);
int usb_get_period(struct usbdevice_s *usbdev,
                   struct usb_endpoint_descriptor *epdesc);
int usb_xfer_time(struct usb_pipe *pipe, int datalen);
struct usb_endpoint_descriptor *usb_find_desc(struct usbdevice_s *usbdev,
                                               int type, int dir);

/* Enumeration */
void usb_enumerate(struct usbhub_s *hub);
void usb_setup(void);

/* DMA helpers - see usb_dma.h (usb_dma_alloc/usb_dma_to_bus/etc.) */

/* General allocation (boot-time, never really freed) */
void *usb_alloc(size_t size);
void usb_free(void *ptr);

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - __builtin_offsetof(type, member)))

#define barrier() asm volatile ("" ::: "memory")

static inline int __fls(u32 word)
{
    int bit = -1;
    while (word) {
        bit++;
        word >>= 1;
    }
    return bit;
}

#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

#endif
