// uhci.c - UHCI (USB1) host controller driver.
//
// Register-level algorithms and data structures derived from the Linux
// kernel UHCI host driver (linux/drivers/usb/host/uhci-*.c, GPL-2.0).
// Adapted to xkern's compact, poll-based, freestanding driver model that
// shares the same usb_pipe / usb_poll_intr API as the OHCI/EHCI/XHCI
// drivers so hub/hid/DE keep working unchanged.

#include "uhci.h"
#include "IOPCIFamily/pci.h"
#include "io.h"
#include "string.h"
#include "klog.h"
#include "klibc.h"
#include "../core/usb.h"
#include "../hub/usbhub.h"

/****************************************************************
 * UHCI register offsets (I/O mapped) and bit definitions
 * (from Linux drivers/usb/host/uhci-hcd.h)
 ****************************************************************/
#define UHCI_USBCMD        0
#define   USBCMD_RS        0x0001   /* Run/Stop */
#define   USBCMD_HCRESET   0x0002   /* Host reset */
#define   USBCMD_GRESET    0x0004   /* Global reset */
#define   USBCMD_CF        0x0040   /* Config Flag (sw only) */
#define   USBCMD_MAXP      0x0080   /* Max Packet (0 = 32, 1 = 64) */
#define UHCI_USBSTS        2
#define   USBSTS_USBINT    0x0001
#define   USBSTS_HSE       0x0008   /* Host System Error */
#define   USBSTS_HCPE      0x0010   /* Host Controller Process Error */
#define   USBSTS_HCH       0x0020   /* HC Halted */
#define UHCI_USBINTR       4
#define UHCI_USBFRNUM      6
#define UHCI_USBFLBASEADD  8
#define UHCI_USBSOF        12
#define UHCI_USBPORTSC1    16

#define   PORTSC_CCS       0x0001   /* Current Connect Status */
#define   PORTSC_CSC       0x0002   /* Connect Status Change */
#define   PORTSC_PE        0x0004   /* Port Enable */
#define   PORTSC_PEC       0x0008   /* Port Enable Change */
#define   PORTSC_DPLUS     0x0010   /* D+ high (line status) */
#define   PORTSC_DMINUS    0x0020   /* D- high (line status) */
#define   PORTSC_RD        0x0040   /* Resume Detect */
#define   PORTSC_RES1      0x0080   /* reserved, always 1 (real port) */
#define   PORTSC_LSDA      0x0100   /* Low Speed Device Attached */
#define   PORTSC_PR        0x0200   /* Port Reset */
#define   PORTSC_SUSP      0x1000   /* Suspend */

#define UHCI_PCI_CLASS      0x0C
#define UHCI_PCI_SUBCLASS  0x03
#define UHCI_PCI_PROGIF     0x00

/* TD status bits (Linux uhci-hcd.h) */
#define TD_CTRL_SPD        (1 << 29)   /* Short Packet Detect */
#define TD_CTRL_C_ERR(v)   ((v) << 27) /* Error Counter */
#define TD_CTRL_LS         (1 << 26)   /* Low Speed Device */
#define TD_CTRL_IOC        (1 << 24)   /* Interrupt on Complete */
#define TD_CTRL_ACTIVE     (1 << 23)   /* TD Active */
#define TD_CTRL_STALLED    (1 << 22)
#define TD_CTRL_DBUFERR    (1 << 21)
#define TD_CTRL_BABBLE     (1 << 20)
#define TD_CTRL_NAK        (1 << 19)
#define TD_CTRL_CRCTIMEO   (1 << 18)
#define TD_CTRL_BITSTUFF   (1 << 17)
#define TD_CTRL_ACTLEN_MASK 0x7FF
#define TD_ERR_MASK        (TD_CTRL_STALLED|TD_CTRL_DBUFERR|TD_CTRL_BABBLE| \
                            TD_CTRL_CRCTIMEO|TD_CTRL_BITSTUFF)

/* TD token bits */
#define TD_TOKEN_DEVADDR_SHIFT 8
#define TD_TOKEN_EP_SHIFT      15
#define TD_TOKEN_TOGGLE_SHIFT  19
#define TD_TOKEN_TOGGLE        (1 << 19)
#define TD_TOKEN_EXPLEN_SHIFT  21
#define TD_TOKEN_EXPLEN_MASK   0x7FF
#define TD_TOKEN_PID_MASK      0xFF

#define USB_PID_OUT    0xE1
#define USB_PID_IN     0x69
#define USB_PID_SETUP  0x2D

#define UHCI_PTR_TERM  1
#define UHCI_PTR_QH    2

#define UHCI_NUMFRAMES 1024

#define uhci_explen(len) ((((len) - 1) & TD_TOKEN_EXPLEN_MASK) \
                          << TD_TOKEN_EXPLEN_SHIFT)
#define uhci_actual(len) (((len) + 1) & TD_TOKEN_EXPLEN_MASK)

/****************************************************************
 * Hardware structures (16-byte aligned)
 ****************************************************************/
struct uhci_td {
    u32 link;       /* next TD / QH, or TERM */
    u32 status;
    u32 token;
    u32 buffer;     /* bus address of data */
    /* software fields */
    dma_addr_t dma;
    struct uhci_td *next;
} __attribute__((aligned(16)));

struct uhci_qh {
    u32 link;       /* next QH, or TERM */
    u32 element;    /* first TD, or TERM */
    /* software fields */
    dma_addr_t dma;
} __attribute__((aligned(16)));

struct uhci_ctl {
    struct usb_s usb;
    u16 io;
    u32 *frame;
    dma_addr_t frame_dma;
    struct uhci_qh *async_qh;
    struct uhci_qh *int_head;
    int nports;
    u8 bus, dev, func;
};

struct uhci_pipe {
    struct usb_pipe pipe;
    struct uhci_qh qh;        /* used for interrupt endpoints */
    struct uhci_td *td;       /* interrupt TD */
    void *data;
    dma_addr_t data_dma;
    u8 toggle;
    u8 lowspeed;
};

/****************************************************************
 * Accessors / helpers
 ****************************************************************/
static inline struct uhci_ctl *uhci_of(struct usb_s *usb)
{
    return container_of(usb, struct uhci_ctl, usb);
}

static u16 urd(struct uhci_ctl *c, u16 reg) { return inw(c->io + reg); }
static void uwr(struct uhci_ctl *c, u16 reg, u16 val) { outw(c->io + reg, val); }

static void uhci_push_td(struct uhci_td *td)
{
    usb_dma_cache_sync(td, sizeof(*td), USB_DMA_TO_DEVICE);
    usb_dma_wmb();
}

static void uhci_pull_td(struct uhci_td *td)
{
    usb_dma_cache_inv(td, sizeof(*td));
}

static struct uhci_td *uhci_alloc_td(void)
{
    struct uhci_td *td = usb_dma_alloc_aligned(sizeof(*td), 16);
    if (td) {
        klibc.memset(td, 0, sizeof(*td));
        td->dma = usb_dma_to_bus(td);
        td->link = UHCI_PTR_TERM;
    }
    return td;
}

static void uhci_fill_td(struct uhci_td *td, u32 status, u32 token,
                         dma_addr_t buf)
{
    td->status = status;
    td->token  = token;
    td->buffer = (u32)buf;
    td->link   = UHCI_PTR_TERM;
}

static u32 uhci_tok(u8 devaddr, u8 ep, u8 toggle, u8 pid, int len)
{
    u32 t = (devaddr << TD_TOKEN_DEVADDR_SHIFT) |
            (ep << TD_TOKEN_EP_SHIFT) |
            (toggle ? TD_TOKEN_TOGGLE : 0) |
            (pid & TD_TOKEN_PID_MASK);
    if (len > 0)
        t |= uhci_explen(len);
    else
        t |= (0x7FF << TD_TOKEN_EXPLEN_SHIFT); /* 0 bytes */
    return t;
}

/****************************************************************
 * Controller bring-up
 ****************************************************************/
static void uhci_reset(struct uhci_ctl *c)
{
    uwr(c, UHCI_USBCMD, USBCMD_HCRESET);
    u32 end = usb_now_ms() + 500;
    while (urd(c, UHCI_USBCMD) & USBCMD_HCRESET) {
        if (usb_now_ms() >= end) {
            klog("UHCI", "reset timed out");
            return;
        }
        usb_msleep(1);
    }
}

static void uhci_start(struct uhci_ctl *c)
{
    /* Frame list -> async QH (which carries control/bulk and links to
     * the interrupt QHs). */
    for (int i = 0; i < UHCI_NUMFRAMES; i++)
        c->frame[i] = (u32)c->async_qh->dma | UHCI_PTR_QH;
    usb_dma_cache_sync(c->frame, UHCI_NUMFRAMES * 4, USB_DMA_TO_DEVICE);

    c->async_qh->link    = UHCI_PTR_TERM;
    c->async_qh->element = UHCI_PTR_TERM;
    usb_dma_cache_sync(c->async_qh, sizeof(*c->async_qh), USB_DMA_TO_DEVICE);
    usb_dma_wmb();

    outl((u32)c->frame_dma, c->io + UHCI_USBFLBASEADD);
    uwr(c, UHCI_USBINTR, 0);          /* polled: no interrupts */
    uwr(c, UHCI_USBCMD, USBCMD_RS | USBCMD_CF | USBCMD_MAXP);
}

static int uhci_probe_ports(struct uhci_ctl *c)
{
    int n = 0;
    for (int p = 0; p < 8; p++) {
        u16 v = urd(c, UHCI_USBPORTSC1 + 2 * p);
        if (v & PORTSC_RES1)          /* "always 1" reserved bit => real */
            n = p + 1;
    }
    return n;
}

/****************************************************************
 * Root hub operations
 ****************************************************************/
static int uhci_hub_detect(struct usbhub_s *hub, u32 port)
{
    struct uhci_ctl *c = uhci_of(hub->cntl);
    return (urd(c, UHCI_USBPORTSC1 + 2 * port) & PORTSC_CCS) ? 1 : 0;
}

static void uhci_hub_disconnect(struct usbhub_s *hub, u32 port)
{
    struct uhci_ctl *c = uhci_of(hub->cntl);
    uwr(c, UHCI_USBPORTSC1 + 2 * port, PORTSC_CCS | PORTSC_LSDA);
}

static int uhci_hub_reset(struct usbhub_s *hub, u32 port)
{
    struct uhci_ctl *c = uhci_of(hub->cntl);
    u16 reg = UHCI_USBPORTSC1 + 2 * port;

    uwr(c, reg, PORTSC_PR);
    u32 end = usb_now_ms() + USB_TIME_DRSTR * 2;
    for (;;) {
        u16 sts = urd(c, reg);
        if (!(sts & PORTSC_PR))
            break;
        if (usb_now_ms() >= end) {
            klog("UHCI", "port %d reset timeout", port);
            return -1;
        }
        usb_msleep(1);
    }
    u16 sts = urd(c, reg);
    if ((sts & (PORTSC_CCS | PORTSC_PE)) != (PORTSC_CCS | PORTSC_PE))
        return -1;
    return (sts & PORTSC_LSDA) ? 1 : 0;
}

static struct usbhub_op_s uhci_HubOp = {
    .detect     = uhci_hub_detect,
    .reset      = uhci_hub_reset,
    .disconnect = uhci_hub_disconnect,
};

static int check_uhci_ports(struct uhci_ctl *c)
{
    struct usbhub_s hub;
    klibc.memset(&hub, 0, sizeof(hub));
    hub.cntl      = &c->usb;
    hub.portcount = c->nports;
    hub.op        = &uhci_HubOp;
    usb_enumerate(&hub);
    return hub.devcount;
}

/****************************************************************
 * Pipe management
 ****************************************************************/
struct usb_pipe *uhci_realloc_pipe(struct usbdevice_s *usbdev,
                                   struct usb_pipe *upipe,
                                   struct usb_endpoint_descriptor *epdesc)
{
    struct uhci_pipe *p;

    if (!upipe) {
        p = usb_dma_alloc_aligned(sizeof(*p), 16);
        if (!p)
            return NULL;
        klibc.memset(p, 0, sizeof(*p));
        usb_desc2pipe(&p->pipe, usbdev, epdesc);
        p->toggle   = 0;
        p->lowspeed = (usbdev->speed == USB_LOWSPEED);
    } else {
        p = container_of(upipe, struct uhci_pipe, pipe);
    }

    if ((epdesc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
            == USB_ENDPOINT_XFER_INT) {
        /* Build a dedicated QH + TD for interrupt polling. */
        if (!p->td) {
            p->td = uhci_alloc_td();
            if (!p->td)
                return NULL;
            p->data = usb_dma_alloc(p->pipe.maxpacket);
            if (!p->data)
                return NULL;
            p->data_dma = usb_dma_to_bus(p->data);

            u8 ep = epdesc->bEndpointAddress & 0x0f;
            p->td->token = uhci_tok(p->pipe.devaddr, ep, 0,
                                    USB_PID_IN, p->pipe.maxpacket);
            p->td->status = TD_CTRL_ACTIVE | TD_CTRL_C_ERR(3) |
                            (p->lowspeed ? TD_CTRL_LS : 0);
            p->td->buffer = (u32)p->data_dma;
            usb_dma_cache_sync(p->td, sizeof(*p->td), USB_DMA_TO_DEVICE);

            p->qh.element = (u32)p->td->dma;
            p->qh.link = p->qh.dma ? 0 : 0;

            /* Link into the schedule after the async QH. */
            struct uhci_ctl *c = uhci_of(usbdev->hub->cntl);
            p->qh.link = c->int_head ? (u32)c->int_head->dma | UHCI_PTR_QH
                                     : UHCI_PTR_TERM;
            usb_dma_cache_sync(&p->qh, sizeof(p->qh), USB_DMA_TO_DEVICE);
            usb_dma_wmb();
            c->int_head = &p->qh;
            c->async_qh->link = (u32)p->qh.dma | UHCI_PTR_QH;
            usb_dma_cache_sync(c->async_qh, sizeof(*c->async_qh),
                               USB_DMA_TO_DEVICE);
            usb_dma_wmb();
        }
    }
    return &p->pipe;
}

/****************************************************************
 * Control / bulk transfers (shared async QH)
 ****************************************************************/
static int uhci_wait_td(struct uhci_ctl *c, struct uhci_td *last, int to_ms)
{
    u32 end = usb_now_ms() + to_ms;
    for (;;) {
        uhci_pull_td(last);
        if (!(last->status & TD_CTRL_ACTIVE))
            return 0;
        if (usb_now_ms() >= end)
            return -1;
        usb_msleep(1);
    }
}

int uhci_send_pipe(struct usb_pipe *p, int dir, const void *cmd,
                   void *data, int datasize)
{
    struct uhci_pipe *up = container_of(p, struct uhci_pipe, pipe);
    struct uhci_ctl *c = uhci_of(p->cntl);
    u8 dev = p->devaddr;
    u8 ep = p->ep;
    u8 ls = up->lowspeed ? TD_CTRL_LS : 0;
    int ret = -1;

    struct uhci_td *first = NULL, *last = NULL, *prev = NULL;
    dma_addr_t cmd_dma = 0, data_dma = 0;

    if (cmd) {
        /* control: SETUP + optional DATA + STATUS */
        cmd_dma = usb_dma_to_bus((void *)cmd);
        usb_dma_cache_sync((void *)cmd, 8, USB_DMA_TO_DEVICE);

        struct uhci_td *s = uhci_alloc_td();
        if (!s)
            return -1;
        uhci_fill_td(s, TD_CTRL_ACTIVE | TD_CTRL_C_ERR(3) | ls,
                     uhci_tok(dev, 0, 0, USB_PID_SETUP, 8), cmd_dma);
        first = prev = s;

        if (datasize > 0) {
            data_dma = usb_dma_to_bus(data);
            if (dir & USB_DIR_IN)
                usb_dma_cache_inv(data, datasize);
            else
                usb_dma_cache_sync(data, datasize, USB_DMA_TO_DEVICE);
            struct uhci_td *d = uhci_alloc_td();
            if (!d)
                return -1;
            uhci_fill_td(d, TD_CTRL_ACTIVE | TD_CTRL_C_ERR(3) | ls |
                         TD_CTRL_SPD,
                         uhci_tok(dev, 0, 1,
                                  (dir & USB_DIR_IN) ? USB_PID_IN
                                                    : USB_PID_OUT,
                                  datasize), data_dma);
            s->link = (u32)d->dma;
            prev = d;
        }

        u8 status_pid = (dir & USB_DIR_IN) ? USB_PID_OUT : USB_PID_IN;
        struct uhci_td *st = uhci_alloc_td();
        if (!st)
            return -1;
        uhci_fill_td(st, TD_CTRL_ACTIVE | TD_CTRL_C_ERR(3) | ls |
                     TD_CTRL_IOC,
                     uhci_tok(dev, 0, 1, status_pid, 0), 0);
        prev->link = (u32)st->dma;
        last = st;
    } else {
        /* bulk: one or more DATA TDs */
        data_dma = usb_dma_to_bus(data);
        int rem = datasize;
        int toggle = up->toggle;
        while (rem > 0) {
            int chunk = rem > p->maxpacket ? p->maxpacket : rem;
            if (dir & USB_DIR_IN)
                usb_dma_cache_inv(data + (datasize - rem), chunk);
            else
                usb_dma_cache_sync(data + (datasize - rem), chunk,
                                   USB_DMA_TO_DEVICE);
            struct uhci_td *t = uhci_alloc_td();
            if (!t)
                return -1;
            uhci_fill_td(t, TD_CTRL_ACTIVE | TD_CTRL_C_ERR(3) | ls |
                         TD_CTRL_SPD,
                         uhci_tok(dev, ep, toggle,
                                  (dir & USB_DIR_IN) ? USB_PID_IN
                                                    : USB_PID_OUT,
                                  chunk),
                         data_dma + (datasize - rem));
            if (!first)
                first = t;
            else
                prev->link = (u32)t->dma;
            prev = t;
            last = t;
            rem -= chunk;
            toggle ^= 1;
        }
        up->toggle = toggle;
    }

    if (!first || !last)
        return -1;

    uhci_push_td(first);
    if (first != last)
        uhci_push_td(last);

    c->async_qh->element = (u32)first->dma;
    usb_dma_cache_sync(c->async_qh, sizeof(*c->async_qh), USB_DMA_TO_DEVICE);
    usb_dma_wmb();

    if (uhci_wait_td(c, last, usb_xfer_time(p, datasize)) == 0) {
        uhci_pull_td(last);
        if (last->status & TD_ERR_MASK)
            klog("UHCI", "xfer error status %08x", last->status);
        else
            ret = 0;
        if (dir & USB_DIR_IN)
            usb_dma_cache_inv(data, datasize);
    }

    c->async_qh->element = UHCI_PTR_TERM;
    usb_dma_cache_sync(c->async_qh, sizeof(*c->async_qh), USB_DMA_TO_DEVICE);
    return ret;
}

/****************************************************************
 * Interrupt polling
 ****************************************************************/
int uhci_poll_intr(struct usb_pipe *p, void *data)
{
    struct uhci_pipe *up = container_of(p, struct uhci_pipe, pipe);
    if (!up->td)
        return -1;

    uhci_pull_td(up->td);
    if (up->td->status & TD_CTRL_ACTIVE)
        return -1;                       /* not completed yet */
    if (up->td->status & TD_ERR_MASK) {
        /* re-arm and report no data */
        up->td->status = TD_CTRL_ACTIVE | TD_CTRL_C_ERR(3) |
                          (up->lowspeed ? TD_CTRL_LS : 0);
        up->qh.element = (u32)up->td->dma;
        usb_dma_cache_sync(up->td, sizeof(*up->td), USB_DMA_TO_DEVICE);
        usb_dma_cache_sync(&up->qh, sizeof(up->qh), USB_DMA_TO_DEVICE);
        usb_dma_wmb();
        return -1;
    }

    int len = uhci_actual(up->td->status & TD_CTRL_ACTLEN_MASK);
    if (len > 0) {
        usb_dma_cache_inv(up->data, len);
        klibc.memcpy(data, up->data, len);
    }

    /* re-arm: flip data toggle, keep TD pointed at by the QH */
    up->toggle ^= 1;
    up->td->token = (up->td->token & ~(1 << TD_TOKEN_TOGGLE_SHIFT)) |
                    (up->toggle ? TD_TOKEN_TOGGLE : 0);
    up->td->status = TD_CTRL_ACTIVE | TD_CTRL_C_ERR(3) |
                     (up->lowspeed ? TD_CTRL_LS : 0);
    up->qh.element = (u32)up->td->dma;
    usb_dma_cache_sync(up->td, sizeof(*up->td), USB_DMA_TO_DEVICE);
    usb_dma_cache_sync(&up->qh, sizeof(up->qh), USB_DMA_TO_DEVICE);
    usb_dma_wmb();

    if (len > 0)
        return 0;                        /* data ready */
    return -1;
}

/****************************************************************
 * Setup / discovery
 ****************************************************************/
void uhci_setup(void)
{
    for (int i = 0; i < pci_device_count(); i++) {
        struct pci_device *pd = pci_get_device(i);
        if (!pd || !pd->present)
            continue;
        if (pd->class_code != UHCI_PCI_CLASS ||
            pd->subclass != UHCI_PCI_SUBCLASS ||
            pd->progif != UHCI_PCI_PROGIF)
            continue;

        struct uhci_ctl *c = usb_dma_alloc_aligned(sizeof(*c), 32);
        if (!c)
            break;
        klibc.memset(c, 0, sizeof(*c));
        c->bus  = pd->bus;
        c->dev  = pd->dev;
        c->func = pd->func;
        c->io   = (u16)(pd->bar[0] & 0xFFFC);
        c->usb.type = USB_TYPE_UHCI;

        klog("UHCI", "controller at %02x:%02x.%d (%04x:%04x) io=%04x",
             c->bus, c->dev, c->func, pd->vendor_id, pd->device_id, c->io);

        if (!c->io)
            continue;

        uhci_reset(c);

        c->frame = usb_dma_alloc_aligned(UHCI_NUMFRAMES * 4, 4096);
        if (!c->frame)
            continue;
        c->frame_dma = usb_dma_to_bus(c->frame);

        c->async_qh = usb_dma_alloc_aligned(sizeof(*c->async_qh), 16);
        if (!c->async_qh)
            continue;
        klibc.memset(c->async_qh, 0, sizeof(*c->async_qh));
        c->async_qh->dma = usb_dma_to_bus(c->async_qh);

        uhci_start(c);

        c->nports = uhci_probe_ports(c);
        klog("UHCI", "ports=%d", c->nports);
        if (c->nports == 0)
            continue;

        usb_msleep(100);
        check_uhci_ports(c);
    }
}
