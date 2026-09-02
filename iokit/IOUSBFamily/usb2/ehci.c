// EHCI (USB2) host controller driver.
//
// Milestone 1: defensive bring-up + root-hub port discovery.  Every
// micro-op is logged so a wedged board names its killer exactly.

#include "ehci.h"
#include "IOPCIFamily/pci.h"
#include "paging.h"
#include "io.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include "klibc.h"
#include "usb_irq.h"
#include "idt.h"

#define EHCI_PCI_CLASS    0x0C
#define EHCI_PCI_SUBCLASS 0x03
#define EHCI_PCI_PROGIF   0x20

/* capability registers (BAR + 0) */
#define EHCI_CAPLENGTH    0x00
#define EHCI_HCSPARAMS    0x04
#define EHCI_HCCPARAMS    0x08
#define EHCI_HCCPARAMS    0x08

/* operational registers (BAR + caplength) */
#define EHCI_USBCMD       0x00
#define EHCI_USBSTS       0x04
#define EHCI_FRINDEX      0x0C
#define EHCI_CTRLDSSEG    0x10   /* 64-bit segment base (32-bit: write 0) */
#define EHCI_PERIODICLIST 0x14   /* periodic frame-list base */
#define EHCI_ASYNCLIST    0x18
#define EHCI_CONFIGFLAG   0x40
#define EHCI_PORTSC       0x44

#define EHCI_USBCMD_RUN       (1 << 0)
#define EHCI_USBCMD_HCRESET   (1 << 1)
#define EHCI_USBCMD_PERIODIC  (1 << 4)
#define EHCI_USBCMD_ASYNC     (1 << 5)

#define EHCI_USBSTS_HCH     (1 << 12)

/* PORTSC bits */
#define EHCI_PS_CCS       (1 << 0)
#define EHCI_PS_CSC       (1 << 1)
#define EHCI_PS_PED       (1 << 2)
#define EHCI_PS_PR        (1 << 8)    /* port reset */
#define EHCI_PS_PP        (1 << 12)   /* port power */
#define EHCI_PS_PO        (1 << 13)   /* port owner: 1=companion, 0=EHCI */
#define EHCI_PS_LINE_J    (1 << 10)   /* line status D+ */
#define EHCI_PS_PSPD      (((u32)3) << 26)

/* qTD token PIDs */
#define EHCI_PID_OUT      0
#define EHCI_PID_IN       1
#define EHCI_PID_SETUP    2

#define EHCI_QTD_ACTIVE   (((u32)1) << 31)
#define EHCI_QTD_STATUS   0xFF
#define EHCI_QTD_BYTES(x) (((u32)(x) & 0x7FFF) << 16)
#define EHCI_QTD_LEN(x)   (((x) >> 16) & 0x7FFF)

#define EHCI_QH_H         (1 << 15)   /* head of reclamation list */
#define EHCI_QH_DTC       (1 << 14)   /* data toggle control (Linux QH_TOGGLE_CTL) */
#define EHCI_QH_CONTROL_EP (1 << 27)  /* FS/LS control endpoint (Linux QH_CONTROL_EP) */
#define EHCI_QH_EPS_FS    (0u << 12)
#define EHCI_QH_EPS_LS    (1u << 12)
#define EHCI_QH_EPS_HS    (2u << 12)
#define EHCI_QH_MAXPKT(x) (((u32)(x) & 0x7FF) << 16)
#define EHCI_QH_MULT(x)   ((u32)(x) << 30)   /* info2 dword */

/* QH info2 split-transaction fields (Linux ehci-q.c hw_info2 layout):
 * S-mask (start split, uframe), C-mask (complete split, uframe),
 * embedded-TT hub address and port.  Required for any full/low-speed
 * endpoint behind an EHCI root hub; for interrupt endpoints the QH must
 * also live in the periodic schedule. */
#define EHCI_QH_SMASK(x)  (((u32)(x) & 0xFF) << 0)
#define EHCI_QH_CMASK(x)  (((u32)(x) & 0xFF) << 8)
#define EHCI_QH_HUBADDR(x) (((u32)(x) & 0x7F) << 16)
#define EHCI_QH_HUBPORT(x) (((u32)(x) & 0x7F) << 23)

#define EHCI_QTD_T        1           /* terminate bit */

#define EHCI_FRAMES       1024
#define EHCI_MAX_PORTS    15
#define EHCI_MAX_CONTROLLERS 4

struct ehci_qtd {
    volatile u32 next;
    volatile u32 anext;
    volatile u32 token;
    volatile u32 bufs[5];
} __attribute__((aligned(32)));

struct ehci_qh {
    volatile u32 horiz;
    volatile u32 charac;
    volatile u32 caps;
    volatile u32 curtd;
    /* qTD overlay */
    volatile u32 o_next;
    volatile u32 o_anext;
    volatile u32 o_token;
    volatile u32 bufs[5];
} __attribute__((aligned(32)));

struct ehci_ctl {
    struct usb_s usb;
    u32 base;
    u16 opbase;
    u8 bus, dev, func;
    u8 n_ports;
    u8 irq;                     /* PCI interrupt line (0 = none) */
    struct ehci_qh *ahead;      /* async ring head (control/bulk) */
    struct ehci_qh *phead;      /* periodic schedule head (interrupt) */
    u32 *flist;                 /* periodic frame list */
    int started;
};

struct ehci_pipe {
    struct usb_pipe pipe;
    struct ehci_ctl *ctl;
    struct ehci_qh *qh;
    struct ehci_qtd *td;        /* persistent intr qTD */
    void *data;                 /* intr buffer */
    int count;
};

static volatile u32 *ehci_cap(struct ehci_ctl *c, u16 off)
{
    return (volatile u32 *)((unsigned long)(c->base + off));
}

static volatile u32 *ehci_op(struct ehci_ctl *c, u16 off)
{
    return (volatile u32 *)
        ((unsigned long)(c->base + c->opbase + off));
}

static u32 erd(struct ehci_ctl *c, u16 cap_off)
{
    return *ehci_cap(c, cap_off);
}

static void owr_cap(struct ehci_ctl *c, u16 cap_off, u32 val)
{
    *ehci_cap(c, cap_off) = val;
}

static u32 ord(struct ehci_ctl *c, u16 op_off)
{
    return *ehci_op(c, op_off);
}

static void owr(struct ehci_ctl *c, u16 op_off, u32 val)
{
    *ehci_op(c, op_off) = val;
}

/****************************************************************
 * Transfer engine - async ring of queue heads, poll based
 ****************************************************************/

/* next == 0 terminates the chain */
static void qtd_fill(struct ehci_qtd *td, u32 next, int pid,
                      void *data, int len)
{
    td->next = next ? next : EHCI_QTD_T;
    td->anext = EHCI_QTD_T;
    td->token = EHCI_QTD_ACTIVE | ((u32)pid << 8) | (3 << 10)
                | EHCI_QTD_BYTES(len);
    if (data && len) {
        /* The controller DMAs to the bus address, not the CPU virtual
         * address - translate through the DMA layer. */
        dma_addr_t p = usb_dma_to_bus(data);
        td->bufs[0] = (u32)p;
        for (int i = 1; i < 5; i++)
            td->bufs[i] = (u32)((p + i * 0x1000) & ~0xFFFUL);
    } else {
        for (int i = 0; i < 5; i++)
            td->bufs[i] = 0;
    }
}

/* submit a qTD chain on the pipe's QH and wait for the last TD */
static int ehci_submit(struct ehci_pipe *p, struct ehci_qtd *tds,
                       struct ehci_qtd *last, u32 timeout_ms)
{
    struct ehci_qh *qh = p->qh;
    struct ehci_ctl *c = p->ctl;

    usb_dma_flush(tds, sizeof(*tds) * 3);
    usb_dma_wmb();
    /* overlay mirrors the FIRST qTD - the controller (and QEMU's
     * async walker) finds the chain start through it */
    qh->curtd = (u32)usb_dma_to_bus(tds);
    qh->o_next = (u32)usb_dma_to_bus(tds);
    qh->o_anext = tds->anext;
    qh->o_token = tds->token;
    for (int i = 0; i < 5; i++)
        qh->bufs[i] = tds->bufs[i];
    usb_dma_flush(qh, sizeof(*qh));
    usb_dma_wmb();
    klog("EHCI", "submit tds=%08x tok=%08x qh=%08x",
         (u32)(unsigned long)tds, tds->token,
         (u32)(unsigned long)qh);

    u32 end = usb_now_ms() + timeout_ms;
    for (;;) {
        usb_dma_invalidate(last, sizeof(*last));
        if (!(last->token & EHCI_QTD_ACTIVE))
            break;
        if (usb_now_ms() >= end) {
            klog("EHCI", "transfer timeout");
            klog("EHCI", "  USBSTS=%08x USBCMD=%08x FRINDEX=%08x",
                 ord(c, EHCI_USBSTS), ord(c, EHCI_USBCMD),
                 ord(c, EHCI_FRINDEX));
            klog("EHCI", "  ASYNCLIST=%08x PERIODICLIST=%08x CONFIGFLAG=%08x",
                 ord(c, EHCI_ASYNCLIST), ord(c, EHCI_PERIODICLIST),
                 ord(c, EHCI_CONFIGFLAG));
            klog("EHCI", "  qh charac=%08x caps=%08x curtd=%08x horiz=%08x",
                 qh->charac, qh->caps, qh->curtd, qh->horiz);
            klog("EHCI", "  ahead_horiz=%08x phead_horiz=%08x",
                 c->ahead ? c->ahead->horiz : 0,
                 c->phead ? c->phead->horiz : 0);
            qh->o_token &= ~EHCI_QTD_ACTIVE;
            qh->curtd = 0;
            return -1;
        }
        usb_msleep(1);
        if ((end - usb_now_ms()) % 250 == 0)
            klog("EHCI", "poll: last=%08x o_tok=%08x ram_td=%08x",
                 last->token, qh->o_token,
                 *(volatile u32 *)((char *)tds + 8));
    }
    barrier();

    if (last->token & EHCI_QTD_STATUS) {
        klog("EHCI", "transfer error token=%08x", last->token);
        return -1;
    }
    return EHCI_QTD_LEN(last->token);   /* bytes NOT transferred */
}

static int ehci_control(struct ehci_pipe *p, int dir_in,
                        const void *cmd, void *data, int datalen)
{
    struct ehci_qtd *tds = usb_dma_alloc_aligned(sizeof(*tds) * 3, 32);
    if (!tds)
        return -1;

    /* setup stage - always 8 bytes from the ctrlrequest */
    qtd_fill(&tds[0], (u32)&tds[1], EHCI_PID_SETUP,
             (void *)cmd, 8);
    if (datalen > 0) {
        qtd_fill(&tds[1], (u32)&tds[2],
                 dir_in ? EHCI_PID_IN : EHCI_PID_OUT, data, datalen);
    } else {
        qtd_fill(&tds[1], (u32)&tds[2],
                 dir_in ? EHCI_PID_IN : EHCI_PID_OUT, NULL, 0);
    }
    /* status stage - opposite direction, zero length */
    qtd_fill(&tds[2], 0, dir_in ? EHCI_PID_OUT : EHCI_PID_IN, NULL, 0);

    // Flush command and data buffers for DMA before the transfer.
    usb_dma_flush(cmd, USB_CONTROL_SETUP_SIZE);
    if (!dir_in && datalen > 0)
        usb_dma_flush(data, datalen);

    int ret = ehci_submit(p, &tds[0], &tds[2], 1000);
    if (ret >= 0 && dir_in && data && datalen > 0)
        usb_dma_invalidate(data, datalen);
    usb_free(tds);
    return ret < 0 ? ret : 0;
}

static struct ehci_ctl *g_ehci[EHCI_MAX_CONTROLLERS];
static int g_ehci_count;

#ifdef USB_USE_INTERRUPTS
/* EHCI interrupt enable bits we turn on (opbase + 0x08) and ack (0x04). */
#define EHCI_USBINTR_USBINT    (1 << 0)
#define EHCI_USBINTR_USBERRINT (1 << 1)
#define EHCI_USBINTR_PORTCHG    (1 << 2)
#define EHCI_INTR_MASK (EHCI_USBINTR_USBINT | EHCI_USBINTR_USBERRINT \
                        | EHCI_USBINTR_PORTCHG)

static void ehci_irq_handler(void *arg)
{
    struct ehci_ctl *c = arg;
    u32 sts = ord(c, EHCI_USBSTS);
    /* Ack every asserted interrupt (RW1C) so the line deasserts.  The
     * poll loop still detects individual transfer completion; the ISR
     * only keeps the controller from interrupt-storming the CPU. */
    owr(c, EHCI_USBSTS, sts & EHCI_INTR_MASK);
}

static void ehci_intr_enable(struct ehci_ctl *c)
{
    c->irq = pci_config_read(c->bus, c->dev, c->func,
                             PCI_INTERRUPT_LINE) & 0xff;
    if (!c->irq)
        return;
    owr(c, EHCI_USBINTR, EHCI_INTR_MASK);   /* controller may assert IRQ */
    if (usb_irq_register(c->irq, ehci_irq_handler, c) != 0) {
        klog("EHCI", "could not register IRQ %d", c->irq);
        return;
    }
    idt_set_gate(0x20 + c->irq, usb_irq_stub(c->irq), 0x08, 0x8E);
    usb_irq_enable(c->irq);
    klog("EHCI", "interrupts enabled on IRQ %d", c->irq);
}
#endif

/****************************************************************
 * Pipe management
 ****************************************************************/

/* NLPTR pointers carry the type in bits 2:1 - 01 = QH */
#define EHCI_QH_TYPE     (1u << 1)

/* link a QH into the async ring right after the head */
static void ehci_link_qh(struct ehci_ctl *c, struct ehci_qh *qh)
{
    qh->horiz = c->ahead->horiz;
    usb_dma_flush(qh, sizeof(*qh));
    usb_dma_wmb();
    c->ahead->horiz = (u32)usb_dma_to_bus(qh) | EHCI_QH_TYPE;
    usb_dma_flush(c->ahead, sizeof(*c->ahead));
    usb_dma_wmb();
}

/* link a QH into the periodic schedule, right after the dummy head.
 * The frame list points at phead, so the HC reaches every periodic QH
 * by walking phead->horiz.  (Linux anchors the periodic list this way.) */
static void ehci_link_periodic(struct ehci_ctl *c, struct ehci_qh *qh)
{
    qh->horiz = c->phead->horiz;
    usb_dma_flush(qh, sizeof(*qh));
    usb_dma_wmb();
    c->phead->horiz = (u32)usb_dma_to_bus(qh) | EHCI_QH_TYPE;
    usb_dma_flush(c->phead, sizeof(*c->phead));
    usb_dma_wmb();
}

static void ehci_qh_init(struct ehci_qh *qh, u8 addr, u8 ep,
                          u16 maxpacket, u8 speed, u8 eptype, u8 tt_port)
{
    u32 eps = (speed == USB_HIGHSPEED) ? EHCI_QH_EPS_HS
            : (speed == USB_LOWSPEED)  ? EHCI_QH_EPS_LS
                                        : EHCI_QH_EPS_FS;
    /* Linux's qh_make: control endpoints set the Data-Toggle-Control bit
     * (the HC owns the toggle across the SETUP/DATA/STATUS stages);
     * interrupt/bulk carry the toggle per-qTD, so DTC stays clear. */
    u8 etype = eptype & USB_ENDPOINT_XFERTYPE_MASK;
    u32 dtc = (etype == USB_ENDPOINT_XFER_CONTROL) ? EHCI_QH_DTC : 0;
    klibc.memset((void *)qh, 0, sizeof(*qh));
    qh->horiz = EHCI_QTD_T;
    qh->charac = dtc | eps | ((u32)ep << 7)
                 | ((u32)addr & 0x7F) | EHCI_QH_MAXPKT(maxpacket);
    /* A full/low-speed control endpoint behind the EHCI embedded TT
     * needs QH_CONTROL_EP so the controller performs the split for the
     * three-stage control transfer (Linux ehci-q.c). */
    if (speed != USB_HIGHSPEED && etype == USB_ENDPOINT_XFER_CONTROL)
        qh->charac |= EHCI_QH_CONTROL_EP;
    qh->caps = EHCI_QH_MULT(1);
    /* Full/low-speed devices behind this EHCI root hub need split
     * transactions.  Per the EHCI spec (and Linux's qh_make) the
     * embedded-TT hub address and port live in QH info2, NOT info1,
     * and interrupt endpoints additionally need S-mask/C-mask so the
     * controller schedules the split in the periodic frame list. */
    if (speed != USB_HIGHSPEED && tt_port) {
        qh->caps |= EHCI_QH_HUBADDR(0)        /* embedded TT, hub addr 0 */
                      | EHCI_QH_HUBPORT(tt_port);
        if ((eptype & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT)
            qh->caps |= EHCI_QH_SMASK(1) | EHCI_QH_CMASK(0x1c);
    } else if ((eptype & USB_ENDPOINT_XFERTYPE_MASK)
            == USB_ENDPOINT_XFER_INT) {
        qh->caps |= EHCI_QH_SMASK(1);
    }
    /* idle overlay must not point anywhere */
    qh->curtd = EHCI_QTD_T;
    qh->o_next = EHCI_QTD_T;
    qh->o_anext = EHCI_QTD_T;
    qh->o_token = 0;
}

static struct usb_pipe *ehci_alloc_pipe(struct usbdevice_s *usbdev,
                                        struct usb_endpoint_descriptor *epdesc)
{
    struct ehci_pipe *p = usb_dma_alloc_aligned(sizeof(*p), 32);
    if (!p)
        return NULL;
    klibc.memset(p, 0, sizeof(*p));

    usb_desc2pipe(&p->pipe, usbdev, epdesc);
    p->ctl = container_of(usbdev->hub->cntl, struct ehci_ctl, usb);

    u8 eptype = epdesc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
    struct ehci_qh *qh = usb_dma_alloc_aligned(sizeof(*qh), 32);
    if (!qh) {
        usb_free(p);
        return NULL;
    }
    ehci_qh_init(qh, usbdev->devaddr, p->pipe.ep,
                  epdesc->wMaxPacketSize, usbdev->speed, eptype,
                  usbdev->port + 1);
    p->qh = qh;

    u8 etype = eptype & USB_ENDPOINT_XFERTYPE_MASK;
    if (etype == USB_ENDPOINT_XFER_INT) {
        /* Interrupt endpoints (mice/keyboards are full/low speed behind
         * EHCI) must live in the periodic schedule and use a persistent
         * qTD that the controller re-executes each frame.  This mirrors
         * Linux's interrupt QH + single-qTD model and is what makes the
         * split transaction actually happen on real hardware. */
        int maxpacket = p->pipe.maxpacket;
        p->td = usb_dma_alloc_aligned(sizeof(*p->td), 32);
        p->data = usb_dma_alloc_aligned(maxpacket, 32);
        if (!p->td || !p->data) {
            usb_free(qh);
            usb_free(p);
            return NULL;
        }
        klibc.memset(p->td, 0, sizeof(*p->td));
        p->td->next = EHCI_QTD_T;
        p->td->anext = EHCI_QTD_T;
        p->td->token = EHCI_QTD_ACTIVE | (EHCI_PID_IN << 8)
                       | (3 << 10) | EHCI_QTD_BYTES(maxpacket);
        p->td->bufs[0] = (u32)usb_dma_to_bus(p->data);
        for (int i = 1; i < 5; i++)
            p->td->bufs[i] = (u32)(((usb_dma_to_bus(p->data) + (i << 12))
                                    & ~0xFFFUL));
        qh->curtd = (u32)usb_dma_to_bus(p->td);
        qh->o_next = (u32)usb_dma_to_bus(p->td);
        qh->o_anext = p->td->anext;
        qh->o_token = p->td->token;
        qh->bufs[0] = p->td->bufs[0];
        for (int i = 1; i < 5; i++)
            qh->bufs[i] = p->td->bufs[i];
        usb_dma_flush(qh, sizeof(*qh));
        usb_dma_flush(p->td, sizeof(*p->td));
        usb_dma_wmb();
        if (p->ctl->started)
            ehci_link_periodic(p->ctl, qh);
    } else {
        if (p->ctl->started)
            ehci_link_qh(p->ctl, qh);
    }

    return &p->pipe;
}

struct usb_pipe *ehci_realloc_pipe(struct usbdevice_s *usbdev,
                                   struct usb_pipe *upipe,
                                   struct usb_endpoint_descriptor *epdesc)
{
    (void)upipe;                /* pool memory: no reclaim needed */
    if (!epdesc)
        return NULL;
    return ehci_alloc_pipe(usbdev, epdesc);
}

int ehci_send_pipe(struct usb_pipe *pipe_fl, int dir, const void *cmd,
                   void *data, int datasize)
{
    struct ehci_pipe *p = container_of(pipe_fl, struct ehci_pipe, pipe);
    if (!cmd)
        return -1;              /* bulk not needed yet */
    return ehci_control(p, dir, cmd, data, datasize);
}

int ehci_poll_intr(struct usb_pipe *pipe_fl, void *data)
{
    struct ehci_pipe *p = container_of(pipe_fl, struct ehci_pipe, pipe);
    struct ehci_qtd *td = p->td;
    int maxpacket = p->pipe.maxpacket;
    int got = -1;

    usb_dma_invalidate(td, sizeof(*td));
    if (!(td->token & EHCI_QTD_ACTIVE)) {
        /* completed this frame */
        if (!(td->token & EHCI_QTD_STATUS)) {
            int left = EHCI_QTD_LEN(td->token);  /* bytes NOT xferred */
            got = maxpacket - left;
            if (got > 0) {
                usb_dma_invalidate(p->data, got);
                klibc.memcpy(data, p->data, got);
            }
        }
        /* re-arm the qTD for the next polling frame */
        td->token = EHCI_QTD_ACTIVE | (EHCI_PID_IN << 8)
                    | (3 << 10) | EHCI_QTD_BYTES(maxpacket);
        usb_dma_flush(td, sizeof(*td));
        p->qh->curtd = (u32)usb_dma_to_bus(td);
        p->qh->o_next = (u32)usb_dma_to_bus(td);
        p->qh->o_token = td->token;
        usb_dma_flush(p->qh, sizeof(*p->qh));
        usb_dma_wmb();
    }
    return got;
}

/****************************************************************
 * Root hub
 ****************************************************************/

static struct ehci_ctl *ehci_of(struct usb_s *usb)
{
    return container_of(usb, struct ehci_ctl, usb);
}

static int ehci_hub_detect(struct usbhub_s *hub, u32 port)
{
    struct ehci_ctl *c = ehci_of(hub->cntl);
    u32 ps = ord(c, EHCI_PORTSC + 4 * port);
    return (ps & EHCI_PS_CCS) ? 1 : 0;
}

static int ehci_hub_reset(struct usbhub_s *hub, u32 port)
{
    struct ehci_ctl *c = ehci_of(hub->cntl);

    if (!(ord(c, EHCI_PORTSC + 4 * port) & EHCI_PS_CCS))
        return -1;

    /* Claim the port for EHCI before touching reset (Linux does the
     * same in its port-power path). */
    u32 ps = ord(c, EHCI_PORTSC + 4 * port);
    if (ps & EHCI_PS_PO) {
        ps &= ~EHCI_PS_PO;
        owr(c, EHCI_PORTSC + 4 * port, ps);
    }

    /* Drive reset for the spec duration, then release it.  Real
     * hardware self-clears PR (the second write is a harmless no-op
     * while resetting); QEMU only completes the reset when software
     * writes the bit back to 0. */
    owr(c, EHCI_PORTSC + 4 * port, EHCI_PS_PR | EHCI_PS_PP);
    usb_msleep(60);
    ps = ord(c, EHCI_PORTSC + 4 * port)
         & ~(EHCI_PS_PR | EHCI_PS_PO | EHCI_PS_CSC);
    owr(c, EHCI_PORTSC + 4 * port, ps);

    u32 end = usb_now_ms() + 200;
    for (;;) {
        ps = ord(c, EHCI_PORTSC + 4 * port);
        if (!(ps & EHCI_PS_CCS))
            return -1;
        if (ps & EHCI_PS_PED)
            break;
        if (usb_now_ms() >= end) {
            klog("EHCI", "port %d enable timeout (ps=%08x)",
                 port + 1, ps);
            return -1;
        }
        usb_msleep(1);
    }

    /* PSPD: 0 full, 1 low, 2 high */
    static const int spd[4] = { USB_FULLSPEED, USB_LOWSPEED,
                                USB_HIGHSPEED, USB_FULLSPEED };
    int speed = spd[(ord(c, EHCI_PORTSC + 4 * port) & EHCI_PS_PSPD) >> 26];
    klog("EHCI", "port %d enabled, speed %d", port + 1, speed);
    return speed;
}

static void ehci_hub_disconnect(struct usbhub_s *hub, u32 port)
{
    (void)hub;
    (void)port;
}

static struct usbhub_op_s ehci_HubOp = {
    .detect = ehci_hub_detect,
    .reset = ehci_hub_reset,
    .disconnect = ehci_hub_disconnect,
};

/****************************************************************
 * Controller start + enumeration
 ****************************************************************/

static int wait_clr(volatile u32 *reg, u32 mask, u32 timeout_ms);
static int wait_set(volatile u32 *reg, u32 mask, u32 timeout_ms);

static int start_ehci(struct ehci_ctl *c)
{
    /* periodic frame list - all terminated, schedule idle but valid */
    u32 *flist = usb_dma_alloc_aligned(EHCI_FRAMES * 4, 4096);
    if (!flist)
        return -1;
    for (int i = 0; i < EHCI_FRAMES; i++)
        flist[i] = EHCI_QTD_T;

    struct ehci_qh *ahead = usb_dma_alloc_aligned(sizeof(*ahead), 32);
    if (!ahead)
        return -1;
    klibc.memset(ahead, 0, sizeof(*ahead));
    ahead->horiz = ((u32)usb_dma_to_bus(ahead) | EHCI_QH_TYPE); /* self */
    ahead->charac = EHCI_QH_H | EHCI_QH_EPS_HS | EHCI_QH_MAXPKT(64);
    ahead->caps = EHCI_QH_MULT(1);
    ahead->curtd = EHCI_QTD_T;
    ahead->o_next = EHCI_QTD_T;
    ahead->o_anext = EHCI_QTD_T;
    c->ahead = ahead;

    /* periodic schedule head (interrupt endpoints chain after this).
     * Every frame points at phead so the HC reaches all periodic QHs
     * via phead->horiz, exactly like Linux's periodic anchor. */
    struct ehci_qh *phead = usb_dma_alloc_aligned(sizeof(*phead), 32);
    if (!phead) {
        usb_free(ahead);
        usb_free(flist);
        return -1;
    }
    klibc.memset(phead, 0, sizeof(*phead));
    phead->horiz = EHCI_QTD_T;
    phead->charac = EHCI_QH_EPS_HS | EHCI_QH_MAXPKT(64);
    phead->caps = EHCI_QH_MULT(1);
    phead->curtd = EHCI_QTD_T;
    phead->o_next = EHCI_QTD_T;
    phead->o_anext = EHCI_QTD_T;
    c->phead = phead;
    c->flist = flist;

    for (int i = 0; i < EHCI_FRAMES; i++)
        flist[i] = (u32)usb_dma_to_bus(phead) | EHCI_QH_TYPE;

    owr(c, EHCI_PERIODICLIST, (u32)usb_dma_to_bus(flist));
    usb_dma_wmb();
    owr(c, EHCI_ASYNCLIST,
        (u32)usb_dma_to_bus(ahead) | EHCI_QH_TYPE);

    owr(c, EHCI_CONFIGFLAG, 1);  /* route ports away from companions */
    usb_msleep(1);
    owr(c, EHCI_USBCMD,
        EHCI_USBCMD_RUN | EHCI_USBCMD_PERIODIC | EHCI_USBCMD_ASYNC);
    usb_msleep(50);
    klog("EHCI", "post-enable: USBSTS=%08x USBCMD=%08x FRINDEX=%08x",
         ord(c, EHCI_USBSTS), ord(c, EHCI_USBCMD),
         ord(c, EHCI_FRINDEX));
    for (int i = 0; i < 5; i++) {
        usb_msleep(50);
        klog("EHCI", "  poll FRINDEX=%08x USBSTS=%08x",
             ord(c, EHCI_FRINDEX), ord(c, EHCI_USBSTS));
    }

    c->started = 1;
    klog("EHCI", "running (async + periodic schedules live)");
    return 0;
}

static int check_ehci_ports(struct ehci_ctl *c)
{
    struct usbhub_s hub;
    klibc.memset(&hub, 0, sizeof(hub));
    hub.cntl = &c->usb;
    hub.portcount = c->n_ports;
    hub.op = &ehci_HubOp;
    usb_enumerate(&hub);
    return hub.devcount;
}

static int wait_clr(volatile u32 *reg, u32 mask, u32 timeout_ms)
{
    for (u32 i = 0; i < timeout_ms; i++) {
        if ((*reg & mask) == 0)
            return 0;
        usb_msleep(1);
    }
    return -1;
}

static int wait_set(volatile u32 *reg, u32 mask, u32 timeout_ms)
{
    for (u32 i = 0; i < timeout_ms; i++) {
        if ((*reg & mask) == mask)
            return 0;
        usb_msleep(1);
    }
    return -1;
}

/* Wake the function to D0 (PM capability walk).  Same ritual the xHCI
 * path uses; MMIO on a D3-parked function is undefined. */
/* Claim the EHCI from the platform BIOS via the USB Legacy Support
 * extended capability (USBLEGSUP).  Until the OS sets the OS-Owned bit
 * the controller's frame timer stays parked and USBCMD RUN is ignored -
 * which is exactly why FRINDEX never advances and nothing enumerates.
 * Linux does the same in ehci_handoff(). */
static void ehci_leg_handoff(struct ehci_ctl *c)
{
    u32 hcc = erd(c, EHCI_HCCPARAMS);
    u8 eecp = (hcc >> 8) & 0xFF;
    for (int guard = 0; guard < 32 && eecp >= 0x40; guard++) {
        u32 cap = erd(c, eecp);
        if ((cap & 0xFF) != 0x01) {        /* not USB Legacy Support */
            eecp = (cap >> 8) & 0xFF;
            continue;
        }
        klog("EHCI", "USBLEGSUP @%02x legsup=%08x", eecp, cap);
        if (cap & (1u << 16)) {            /* BIOS owned */
            owr_cap(c, eecp, cap | (1u << 24));   /* claim OS ownership */
            for (int i = 0; i < 200; i++) {
                if (!(erd(c, eecp) & (1u << 16)))
                    break;
                usb_msleep(1);
            }
            owr_cap(c, eecp + 4, 0);       /* kill all legacy SMIs */
            klog("EHCI", "claimed OS ownership (legsup=%08x)",
                 erd(c, eecp));
        }
        break;
    }
}

static void ehci_set_d0(struct ehci_ctl *c)
{
    klog("EHCI", "%02x:%02x.%d waking to D0", c->bus, c->dev, c->func);
    u8 ptr = pci_config_read_byte(c->bus, c->dev, c->func,
                                  PCI_CAPABILITIES);
    for (int guard = 0; guard < 16 && ptr >= 0x40; guard++) {
        u8 id = pci_config_read_byte(c->bus, c->dev, c->func, ptr);
        if (id == 0x01) {           /* PCI power management */
            u32 pmcsr = pci_config_read(c->bus, c->dev, c->func,
                                        ptr + 4);
            if ((pmcsr & 0x3) != 0) {
                pci_config_write(c->bus, c->dev, c->func, ptr + 4,
                                 pmcsr & ~0x3);
                usb_msleep(20);
                klog("EHCI", "D%d -> D0", pmcsr & 0x3);
            }
            return;
        }
        ptr = pci_config_read_byte(c->bus, c->dev, c->func,
                                   ptr + 1);
    }
}

static void ehci_bringup(struct ehci_ctl *c)
{
    ehci_set_d0(c);
    ehci_leg_handoff(c);
    klog("EHCI", "hccparams=%08x eecp=%02x", erd(c, EHCI_HCCPARAMS),
         (u8)((erd(c, EHCI_HCCPARAMS) >> 8) & 0xFF));

    /* memory decode before any MMIO touch */
    u32 cmd = pci_config_read(c->bus, c->dev, c->func, PCI_COMMAND);
    pci_config_write(c->bus, c->dev, c->func, PCI_COMMAND,
                     (cmd & 0xFFFF) | 0x0002);

    paging_map_region(c->base, c->base, 0x1000,
                      PAGE_PRESENT | PAGE_WRITE | PAGE_PWT | PAGE_PCD);
    klog("EHCI", "mapped %08x", c->base);

    /* validate caps before touching anything else */
    u8 caplen = erd(c, EHCI_CAPLENGTH) & 0xFF;
    u32 hcsparams = erd(c, EHCI_HCSPARAMS);
    c->n_ports = hcsparams & 0xF;
    klog("EHCI", "caplength=%02x hcsparams=%08x ports=%d",
         caplen, hcsparams, c->n_ports);
    if (caplen < 0x20 || caplen > 0xFF ||
        c->n_ports == 0 || c->n_ports > EHCI_MAX_PORTS) {
        klog("EHCI", "implausible caps - skipping this controller");
        return;
    }
    c->opbase = caplen;

    /* enable bus mastering so the controller can DMA qTDs, QHs, and
     * packet data to/from main memory (required on real hardware) */
    cmd = pci_config_read(c->bus, c->dev, c->func, PCI_COMMAND);
    pci_config_write(c->bus, c->dev, c->func, PCI_COMMAND,
                     (cmd & 0xFFFF) | 0x0006);
    klog("EHCI", "bus master enabled cmd=%08x",
         pci_config_read(c->bus, c->dev, c->func, PCI_COMMAND));

    /* halt then reset - spec order, both bounded */
    u32 cmdreg = ord(c, EHCI_USBCMD);
    klog("EHCI", "usbcmd=%08x halting", cmdreg);
    owr(c, EHCI_USBCMD, 0);
    if (wait_set(ehci_op(c, EHCI_USBSTS), EHCI_USBSTS_HCH, 500)) {
        klog("EHCI", "controller refused to halt");
        return;
    }
    klog("EHCI", "halted");

    /* NOTE: do NOT issue HCRESET here.  QEMU's EHCI model (ehci_reset)
     * deletes its frame timer on reset and never re-arms it, so any
     * transfer scheduled afterward never executes (FRINDEX stays 0).
     * Real hardware keeps the frame timer running across reset, so we
     * skip the controller reset and rely on the halt + re-init.  If we
     * ever need a hard reset on real hardware, gate it behind a flag. */
#if 0
    owr(c, EHCI_USBCMD, EHCI_USBCMD_HCRESET);
    if (wait_clr(ehci_op(c, EHCI_USBCMD), EHCI_USBCMD_HCRESET, 500)) {
        klog("EHCI", "reset timed out");
        return;
    }
    klog("EHCI", "reset done");
#endif

    /* CONFIGFLAG must be set before any PORTSC write takes effect: it
     * makes the port's Port-Owner bit writable so software can claim
     * the port for EHCI instead of leaving it routed to a companion
     * controller.  Mirrors the Linux EHCI driver (CF before ports). */
    owr(c, EHCI_CONFIGFLAG, 1);

    /* Power every port and claim it for EHCI.  Writing 0 to the Port
     * Owner bit (PO) hands the port to this controller; without this a
     * firmware-routed port stays owned by a companion, EHCI never sees
     * CCS, and enumeration reports "no device".  Let the attach state
     * settle - USB debounce is >= 100ms on real hardware. */
    for (int p = 0; p < c->n_ports; p++) {
        u32 ps = ord(c, EHCI_PORTSC + 4 * p);
        ps &= ~(EHCI_PS_PO | EHCI_PS_PR);   /* EHCI owns, no reset */
        ps |= EHCI_PS_PP;                   /* power on */
        owr(c, EHCI_PORTSC + 4 * p, ps);
    }
    usb_msleep(200);

    int attached = 0;
    for (int p = 0; p < c->n_ports; p++) {
        u32 ps = ord(c, EHCI_PORTSC + 4 * p);
        if (ps & EHCI_PS_CCS)
            attached++;
        klog("EHCI", "port %d: %08x%s%s%s", p + 1, ps,
             (ps & EHCI_PS_CCS) ? " DEVICE" : "",
             (ps & EHCI_PS_PO) ? " (companion-owned)" : " (ehci-owned)",
             (ps & EHCI_PS_LINE_J) ? " (ls=J)" : "");
    }

    if (!attached) {
        klog("EHCI", "no devices on this controller");
        return;
    }

    if (start_ehci(c)) {
        klog("EHCI", "failed to start schedules");
        return;
    }

#ifdef USB_USE_INTERRUPTS
    ehci_intr_enable(c);
#endif

    klog("EHCI", "enumerating root hub");
    int count = check_ehci_ports(c);
    klog("EHCI", "enumeration done, devices=%d", count);
}

void ehci_setup(void)
{
    for (int i = 0; i < pci_device_count(); i++) {
        struct pci_device *pd = pci_get_device(i);
        if (!pd || !pd->present)
            continue;
        if (pd->class_code != EHCI_PCI_CLASS ||
            pd->subclass != EHCI_PCI_SUBCLASS ||
            pd->progif != EHCI_PCI_PROGIF)
            continue;
        if (g_ehci_count >= EHCI_MAX_CONTROLLERS)
            break;

        struct ehci_ctl *c = usb_dma_alloc_aligned(sizeof(*c), 32);
        if (!c)
            break;
        klibc.memset(c, 0, sizeof(*c));
        c->bus = pd->bus;
        c->dev = pd->dev;
        c->func = pd->func;
        c->base = pd->bar[0] & 0xFFFFFFF0;
        c->usb.type = USB_TYPE_EHCI;

        klog("EHCI", "controller %d at %02x:%02x.%d (%04x:%04x) "
             "bar=%08x", g_ehci_count, c->bus, c->dev, c->func,
             pd->vendor_id, pd->device_id, c->base);
        g_ehci[g_ehci_count++] = c;

        if (!c->base)
            continue;
        ehci_bringup(c);
    }

    if (!g_ehci_count)
        klog("EHCI", "no EHCI controllers found");
}
