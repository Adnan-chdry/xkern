#include "ohci.h"
#include "IOPCIFamily/pci.h"
#include "io.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include "paging.h"
#include "../core/usb.h"
#include "../hub/usbhub.h"
#include <stddef.h>
#include "klibc.h"
#include "usb_irq.h"
#include "idt.h"

#define STACKOTDS 18
#define OHCI_TD_ALIGN 16
#define USB_PAGE_SIZE 4096

static struct ohci_controller g_ohci_ctl;

#ifdef USB_USE_INTERRUPTS
static void ohci_irq_handler(void *arg)
{
    struct ohci_controller *ctl = arg;
    /* Every status bit is RW1C: write-1-to-clear acks the interrupt so
     * the line deasserts.  Transfer completion is still observed by the
     * poll loop; the ISR just keeps the controller from storming. */
    ohci_write32(ctl->regs, OHCI_HC_INTSTATUS, 0xFFFFFFFF);
}

static void ohci_intr_enable(struct ohci_controller *ctl)
{
    ctl->irq = pci_config_read(ctl->bus, ctl->dev, ctl->func,
                               PCI_INTERRUPT_LINE) & 0xff;
    if (!ctl->irq)
        return;
    /* Enable the master interrupt and the useful sources. */
    ohci_write32(ctl->regs, OHCI_HC_INTENABLE,
                 OHCI_INT_MIE | 0x00FFFFFF);
    /* HC_CONTROL.MIE gates the interrupt output; without it the
     * controller never asserts the line. */
    u32 ctrl = ohci_read32(ctl->regs, OHCI_HC_CONTROL);
    ohci_write32(ctl->regs, OHCI_HC_CONTROL, ctrl | OHCI_CTRL_MIE);
    if (usb_irq_register(ctl->irq, ohci_irq_handler, ctl) != 0) {
        klog("OHCI", "could not register IRQ %d", ctl->irq);
        return;
    }
    idt_set_gate(0x20 + ctl->irq, usb_irq_stub(ctl->irq), 0x08, 0x8E);
    usb_irq_enable(ctl->irq);
    klog("OHCI", "interrupts enabled on IRQ %d", ctl->irq);
}
#endif

static struct ohci_controller *ohci_of(struct usb_s *usb)
{
    return container_of(usb, struct ohci_controller, usb);
}

u32 ohci_read32(struct ohci_regs *regs, u16 reg)
{
    return *(volatile u32 *)((unsigned long)regs + reg);
}

void ohci_write32(struct ohci_regs *regs, u16 reg, u32 val)
{
    *(volatile u32 *)((unsigned long)regs + reg) = val;
}

/****************************************************************
 * Root hub operations (SeaBIOS usb-ohci.c)
 ****************************************************************/
static int ohci_hub_detect(struct usbhub_s *hub, u32 port)
{
    struct ohci_controller *cntl = ohci_of(hub->cntl);
    u32 sts = ohci_read32(cntl->regs, OHCI_HC_RH_PORT_STATUS + port * 4);
    return (sts & RH_PS_CCS) ? 1 : 0;
}

static void ohci_hub_disconnect(struct usbhub_s *hub, u32 port)
{
    struct ohci_controller *cntl = ohci_of(hub->cntl);
    ohci_write32(cntl->regs, OHCI_HC_RH_PORT_STATUS + port * 4,
                 RH_PS_CCS | RH_PS_LSDA);
}

static int ohci_hub_reset(struct usbhub_s *hub, u32 port)
{
    struct ohci_controller *cntl = ohci_of(hub->cntl);
    ohci_write32(cntl->regs, OHCI_HC_RH_PORT_STATUS + port * 4, RH_PS_PRS);

    u32 sts;
    u32 end = usb_now_ms() + USB_TIME_DRSTR * 2;
    for (;;) {
        sts = ohci_read32(cntl->regs, OHCI_HC_RH_PORT_STATUS + port * 4);
        if (!(sts & RH_PS_PRS))
            break;
        if (usb_now_ms() >= end) {
            klog("OHCI", "port %d reset timeout", port);
            ohci_hub_disconnect(hub, port);
            return -1;
        }
        usb_msleep(1);
    }

    if ((sts & (RH_PS_CCS | RH_PS_PES)) != (RH_PS_CCS | RH_PS_PES))
        return -1;

    return !!(sts & RH_PS_LSDA);
}

static struct usbhub_op_s ohci_HubOp = {
    .detect = ohci_hub_detect,
    .reset = ohci_hub_reset,
    .disconnect = ohci_hub_disconnect,
};

// Find any devices connected to the root hub (poll based).
static int check_ohci_ports(struct ohci_controller *cntl)
{
    struct ohci_regs *regs = cntl->regs;

    u32 rha = ohci_read32(regs, OHCI_HC_RH_DESC_A);
    rha &= ~(RH_A_PSM | RH_A_OCPM);
    ohci_write32(regs, OHCI_HC_RH_STATUS, RH_HS_LPSC);
    ohci_write32(regs, OHCI_HC_RH_DESC_B, RH_B_PPCM);
    usb_msleep((rha >> 24) * 2);

    struct usbhub_s hub;
    klibc.memset(&hub, 0, sizeof(hub));
    hub.cntl = &cntl->usb;
    hub.portcount = rha & RH_A_NDP;
    cntl->ports = hub.portcount;
    hub.op = &ohci_HubOp;
    usb_enumerate(&hub);
    return hub.devcount;
}

/****************************************************************
 * Wait for next USB frame - for safe memory release.
 ****************************************************************/
static void ohci_waittick(struct ohci_regs *regs)
{
    barrier();
    struct ohci_hcca *hcca = (void *)regs->hcca;
    usb_dma_invalidate(hcca, sizeof(*hcca));
    u32 startframe = hcca->frame_no;
    u32 end = usb_now_ms() + 5000;
    for (;;) {
        if (hcca->frame_no != startframe)
            break;
        if (usb_now_ms() >= end)
            return;
        usb_msleep(1);
    }
}

/****************************************************************
 * Controller setup
 ****************************************************************/
static int start_ohci(struct ohci_regs *regs, struct ohci_hcca *hcca)
{
    u32 oldfminterval = ohci_read32(regs, OHCI_HC_FM_INTERVAL);
    u32 oldrwc = ohci_read32(regs, OHCI_HC_CONTROL) & OHCI_CTRL_RWC;

    // Reset controller.
    ohci_write32(regs, OHCI_HC_CONTROL, OHCI_USB_RESET | oldrwc);
    ohci_read32(regs, OHCI_HC_CONTROL);
    usb_msleep(USB_TIME_DRSTR);

    // Software init.
    ohci_write32(regs, OHCI_HC_CMDSTATUS, OHCI_HCR);
    u32 end = usb_now_ms() + 100;
    for (;;) {
        u32 status = ohci_read32(regs, OHCI_HC_CMDSTATUS);
        if (!(status & OHCI_HCR))
            break;
        if (usb_now_ms() >= end) {
            klog("OHCI", "HCR timeout");
            return -1;
        }
    }

    // Init memory.
    ohci_write32(regs, OHCI_HC_CTRL_HEAD_ED, 0);
    ohci_write32(regs, OHCI_HC_BULK_HEAD_ED, 0);
    ohci_write32(regs, OHCI_HC_HCCA, (u32)usb_dma_to_bus(hcca));

    // Init fminterval.
    u32 fi = oldfminterval & 0x3fff;
    ohci_write32(regs, OHCI_HC_FM_INTERVAL,
                 (((oldfminterval & FIT) ^ FIT)
                  | fi | (((6 * (fi - 210)) / 7) << 16)));
    ohci_write32(regs, OHCI_HC_PERIODIC_START, ((9 * fi) / 10) & 0x3fff);
    ohci_read32(regs, OHCI_HC_CONTROL);

    // Go into operational state (no interrupts - poll based).
    ohci_write32(regs, OHCI_HC_CONTROL,
                 (OHCI_CTRL_CBSR | OHCI_CTRL_CLE | OHCI_CTRL_BLE | OHCI_CTRL_PLE
                  | OHCI_USB_OPER | oldrwc));
    ohci_read32(regs, OHCI_HC_CONTROL);

    return 0;
}

void ohci_setup(void)
{
    struct ohci_controller *ctl = &g_ohci_ctl;
    klibc.memset(ctl, 0, sizeof(*ctl));

    u8 bus = 0, dev = 0, func = 0;
    int found = pci_find_class(OHCI_PCI_CLASS, OHCI_PCI_SUBCLASS, OHCI_PCI_PROGIF,
                               &bus, &dev, &func);
    if (found != 0) {
        klog("OHCI", "no OHCI controller found");
        return;
    }

    u32 bar = pci_config_read(bus, dev, func, PCI_BAR0);
    ctl->bar0 = bar & 0xFFFFFFF0;
    ctl->bus = bus;
    ctl->dev = dev;
    ctl->func = func;
    ctl->regs = (struct ohci_regs *)(unsigned long)ctl->bar0;

    paging_map_region(ctl->bar0, ctl->bar0, 0x10000,
                      PAGE_PRESENT | PAGE_WRITE | PAGE_PWT | PAGE_PCD);

    u32 hc_rev = ohci_read32(ctl->regs, OHCI_HC_REVISION);
    klog("OHCI", "controller at PCI %d:%d.%d BAR0 0x%x rev 0x%x",
         bus, dev, func, ctl->bar0, hc_rev);

    u32 cmd = pci_config_read(bus, dev, func, PCI_COMMAND);
    cmd |= (1 << 1) | (1 << 2);
    pci_config_write(bus, dev, func, PCI_COMMAND, cmd);

    // Default: poll based, controller interrupts masked.
    ohci_write32(ctl->regs, OHCI_HC_INTDISABLE, 0xFFFFFFFF);
    ohci_write32(ctl->regs, OHCI_HC_INTSTATUS, 0xFFFFFFFF);

    struct ohci_hcca *hcca = usb_alloc(sizeof(*hcca));
    if (!hcca) {
        klog("OHCI", "no memory for hcca");
        return;
    }
    klibc.memset(hcca, 0, sizeof(*hcca));
    ctl->hcca = hcca;

    struct ohci_ed *skip_ed = usb_alloc(sizeof(*skip_ed));
    if (!skip_ed) {
        klog("OHCI", "no memory for skip_ed");
        return;
    }
    skip_ed->hwINFO = ED_SKIP;
    for (int i = 0; i < 32; i++)
        hcca->int_table[i] = (u32)usb_dma_to_bus(skip_ed);
    usb_dma_flush(hcca, sizeof(*hcca));

    ctl->usb.type = USB_TYPE_OHCI;

    if (start_ohci(ctl->regs, ctl->hcca) != 0) {
        klog("OHCI", "failed to start controller");
        return;
    }

#ifdef USB_USE_INTERRUPTS
    ohci_intr_enable(ctl);
#endif

    klog("OHCI", "controller operational, enumerating root hub");
    int count = check_ohci_ports(ctl);
    klog("OHCI", "enumeration done, devices=%d", count);
}

/****************************************************************
 * End point communication
 ****************************************************************/
static void ohci_desc2pipe(struct ohci_pipe *pipe, struct usbdevice_s *usbdev,
                           struct usb_endpoint_descriptor *epdesc)
{
    usb_desc2pipe(&pipe->pipe, usbdev, epdesc);
    pipe->ed.hwINFO = (ED_SKIP | usbdev->devaddr | (pipe->pipe.ep << 7)
                       | (epdesc->wMaxPacketSize << 16)
                       | (usbdev->speed ? ED_LOWSPEED : 0));
    struct ohci_controller *cntl = ohci_of(usbdev->hub->cntl);
    pipe->regs = cntl->regs;
}

static struct usb_pipe *ohci_alloc_intr_pipe(struct usbdevice_s *usbdev,
                                             struct usb_endpoint_descriptor *epdesc)
{
    struct ohci_controller *cntl = ohci_of(usbdev->hub->cntl);
    int frameexp = usb_get_period(usbdev, epdesc);
    klog("OHCI", "intr pipe frameexp=%d", frameexp);

    if (frameexp > 5)
        frameexp = 5;
    int maxpacket = epdesc->wMaxPacketSize;
    int ms = 1 << frameexp;
    int count = DIV_ROUND_UP(2, ms) + 1;

    struct ohci_pipe *pipe = usb_alloc(sizeof(*pipe));
    struct ohci_td *tds = usb_alloc(sizeof(*tds) * count);
    void *data = usb_alloc(maxpacket * count);
    if (!pipe || !tds || !data)
        return NULL;
    klibc.memset(pipe, 0, sizeof(*pipe));
    ohci_desc2pipe(pipe, usbdev, epdesc);
    pipe->ed.hwINFO &= ~ED_SKIP;
    pipe->data = data;
    pipe->count = count;
    pipe->tds = tds;

    struct ohci_ed *ed = &pipe->ed;
    ed->hwHeadP = (u32)usb_dma_to_bus(&tds[0]);
    ed->hwTailP = (u32)usb_dma_to_bus(&tds[count - 1]);

    for (int i = 0; i < count - 1; i++) {
        tds[i].hwINFO = TD_DP_IN | TD_T_TOGGLE | TD_CC;
        tds[i].hwCBP = (u32)usb_dma_to_bus(data) + maxpacket * i;
        tds[i].hwNextTD = (u32)usb_dma_to_bus(&tds[i + 1]);
        tds[i].hwBE = tds[i].hwCBP + maxpacket - 1;
    }

    struct ohci_hcca *hcca = cntl->hcca;
    if (frameexp == 0) {
        struct ohci_ed *intr_ed = (void *)hcca->int_table[0];
        ed->hwNextED = intr_ed->hwNextED;
        usb_dma_wmb();
        intr_ed->hwNextED = (u32)usb_dma_to_bus(ed);
    } else {
        int startpos = 1 << (frameexp - 1);
        ed->hwNextED = hcca->int_table[startpos];
        usb_dma_wmb();
        for (int i = startpos; i < 32; i += ms)
            hcca->int_table[i] = (u32)usb_dma_to_bus(ed);
    }
    usb_dma_flush(hcca, sizeof(*hcca));
    usb_dma_flush(ed, sizeof(*ed));
    usb_dma_flush(tds, sizeof(*tds) * count);
    usb_dma_wmb();

    return &pipe->pipe;
}

struct usb_pipe *ohci_realloc_pipe(struct usbdevice_s *usbdev,
                                   struct usb_pipe *upipe,
                                   struct usb_endpoint_descriptor *epdesc)
{
    usb_add_freelist(upipe);
    if (!epdesc)
        return NULL;

    u8 eptype = epdesc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
    if (eptype == USB_ENDPOINT_XFER_INT)
        return ohci_alloc_intr_pipe(usbdev, epdesc);

    struct ohci_controller *cntl = ohci_of(usbdev->hub->cntl);

    struct usb_pipe *usbpipe = usb_get_freelist(&cntl->usb, eptype);
    if (usbpipe) {
        struct ohci_pipe *pipe = container_of(usbpipe, struct ohci_pipe, pipe);
        ohci_desc2pipe(pipe, usbdev, epdesc);
        return usbpipe;
    }

    struct ohci_pipe *pipe = usb_alloc(sizeof(*pipe));
    if (!pipe)
        return NULL;
    klibc.memset(pipe, 0, sizeof(*pipe));
    ohci_desc2pipe(pipe, usbdev, epdesc);

    u16 headreg = (eptype == USB_ENDPOINT_XFER_CONTROL)
                       ? OHCI_HC_CTRL_HEAD_ED : OHCI_HC_BULK_HEAD_ED;
    pipe->ed.hwNextED = ohci_read32(cntl->regs, headreg);
    usb_dma_wmb();
    ohci_write32(cntl->regs, headreg, (u32)usb_dma_to_bus(&pipe->ed));
    usb_dma_flush(&pipe->ed, sizeof(pipe->ed));
    return &pipe->pipe;
}

static int wait_ed(struct ohci_ed *ed, int timeout)
{
    u32 end = usb_now_ms() + timeout;
    for (;;) {
        usb_dma_invalidate(ed, sizeof(*ed));
        if ((ed->hwHeadP & ~(ED_C | ED_H)) == ed->hwTailP)
            return 0;
        if (usb_now_ms() >= end) {
            klog("OHCI", "ED timeout: info=0x%x tail=0x%x head=0x%x",
                 ed->hwINFO, ed->hwTailP, ed->hwHeadP);
            return -1;
        }
        usb_msleep(1);
    }
}

int ohci_send_pipe(struct usb_pipe *p, int dir, const void *cmd,
                    void *data, int datasize)
{
    struct ohci_pipe *pipe = container_of(p, struct ohci_pipe, pipe);

    // TDs must live in DMA-able memory below 4 GiB (OHCI is a 32-bit
    // controller), so take them from the DMA pool rather than the stack.
    struct ohci_td *tds = usb_dma_alloc_aligned(sizeof(*tds) * STACKOTDS,
                                                OHCI_TD_ALIGN);
    if (!tds)
        return -1;
    struct ohci_td *td = tds;
    klibc.memset(tds, 0, sizeof(*tds) * STACKOTDS);

    u16 maxpacket = pipe->pipe.maxpacket;
    u32 toggle = 0, statuscmd = OHCI_BLF;
    if (cmd) {
        // Send setup pid on control transfers.
        td->hwINFO = TD_DP_SETUP | TD_T_DATA0 | TD_CC;
        td->hwCBP = (u32)usb_dma_to_bus((void *)cmd);
        td->hwNextTD = (u32)usb_dma_to_bus(&td[1]);
        td->hwBE = (u32)usb_dma_to_bus((void *)cmd) + USB_CONTROL_SETUP_SIZE - 1;
        td++;
        toggle = TD_T_DATA1;
        statuscmd = OHCI_CLF;
    }
    dma_addr_t dest = usb_dma_to_bus(data), dataend = dest + datasize;
    while (dest < dataend) {
        // Send data pids.
        if (td >= &tds[STACKOTDS])
            return -1;
        int maxtransfer = 2 * USB_PAGE_SIZE - (dest & (USB_PAGE_SIZE - 1));
        int transfer = dataend - dest;
        if (transfer > maxtransfer)
            transfer = ALIGN_DOWN(maxtransfer, maxpacket);
        td->hwINFO = (dir ? TD_DP_IN : TD_DP_OUT) | toggle | TD_CC;
        td->hwCBP = (u32)dest;
        td->hwNextTD = (u32)usb_dma_to_bus(&td[1]);
        td->hwBE = (u32)(dest + transfer - 1);
        td++;
        dest += transfer;
    }
    if (cmd) {
        // Send status pid on control transfers.
        if (td >= &tds[STACKOTDS])
            return -1;
        td->hwINFO = (dir ? TD_DP_OUT : TD_DP_IN) | TD_T_DATA1 | TD_CC;
        td->hwCBP = 0;
        td->hwNextTD = (u32)usb_dma_to_bus(&td[1]);
        td->hwBE = 0;
        td++;
    }

    usb_dma_flush(tds, sizeof(*tds) * STACKOTDS);
    if (cmd)
        usb_dma_flush((void *)cmd, USB_CONTROL_SETUP_SIZE);
    if (!dir && datasize)
        usb_dma_flush(data, datasize);
    usb_dma_wmb();

    // Transfer data.
    pipe->ed.hwHeadP = (u32)usb_dma_to_bus(tds) | (pipe->ed.hwHeadP & ED_C);
    pipe->ed.hwTailP = (u32)usb_dma_to_bus(td);
    usb_dma_wmb();
    pipe->ed.hwINFO &= ~ED_SKIP;
    usb_dma_flush(&pipe->ed, sizeof(pipe->ed));
    ohci_write32(pipe->regs, OHCI_HC_CMDSTATUS, statuscmd);
    usb_dma_wmb();

    int ret = wait_ed(&pipe->ed, usb_xfer_time(p, datasize));
    pipe->ed.hwINFO |= ED_SKIP;
    usb_dma_flush(&pipe->ed, sizeof(pipe->ed));
    if (ret)
        ohci_waittick(pipe->regs);
    if (dir && datasize)
        usb_dma_invalidate(data, datasize);
    return ret;
}

int ohci_poll_intr(struct usb_pipe *p, void *data)
{
    struct ohci_pipe *pipe = container_of(p, struct ohci_pipe, pipe);
    usb_dma_invalidate(&pipe->ed, sizeof(pipe->ed));
    usb_dma_invalidate(pipe->tds, sizeof(*pipe->tds) * pipe->count);

    struct ohci_td *tds = pipe->tds;
    struct ohci_td *head = (void *)(pipe->ed.hwHeadP & ~(ED_C | ED_H));
    struct ohci_td *tail = (void *)pipe->ed.hwTailP;
    int count = pipe->count;
    int pos = (tail - tds + 1) % count;
    struct ohci_td *next = &tds[pos];
    if (head == next)
        // No new data.
        return -1;

    int maxpacket = pipe->pipe.maxpacket;
    void *pipedata = pipe->data;
    void *intrdata = pipedata + maxpacket * pos;
    usb_dma_invalidate(intrdata, maxpacket);
    klibc.memcpy(data, intrdata, maxpacket);

    // Re-enable this td.
    tail->hwINFO = TD_DP_IN | TD_T_TOGGLE | TD_CC;
    intrdata = pipedata + maxpacket * (tail - tds);
    tail->hwCBP = (u32)usb_dma_to_bus(intrdata);
    tail->hwNextTD = (u32)usb_dma_to_bus(next);
    tail->hwBE = (u32)usb_dma_to_bus(intrdata) + maxpacket - 1;
    usb_dma_flush(tail, sizeof(*tail));
    usb_dma_wmb();
    pipe->ed.hwTailP = (u32)usb_dma_to_bus(next);
    usb_dma_flush(&pipe->ed, sizeof(pipe->ed));

    return 0;
}
