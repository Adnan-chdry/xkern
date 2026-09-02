// xHCI (USB3) host controller driver - ported from SeaBIOS usb-xhci.c.
//
// Uses the kernel's DMA pool (PMM-backed, < 4 GiB, cache-coherent) and
// TSC timers.  The event ring is drained by the polling loop in
// xhci_event_wait(); with USB_USE_INTERRUPTS the controller's hard IRQ
// also services it via the generic IRQ dispatch in core/usb_irq.c.

#include "xhci.h"
#include "IOPCIFamily/pci.h"
#include "paging.h"
#include "io.h"
#include "acpi/acpi.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include "usb_irq.h"
#include "idt.h"
#include "../core/usb.h"
#include "../hub/usbhub.h"
#include <stddef.h>
#include "klibc.h"

#define XHCI_PCI_CLASS    0x0C
#define XHCI_PCI_SUBCLASS 0x03
#define XHCI_PCI_PROGIF   0x30

/*
 * Freeze-proof progress marker for real boards without a serial port:
 * short numbered lines that survive on screen.  When the machine
 * wedges, the last visible "step N" names the COMPLETED operation -
 * whatever code follows it is the killer.  Steps:
 *   1 probed        2 pm walk start   3 pm cap found    4 D0 written
 *   5 post-msleep   7 intel matched   8 XUSB2PR ok      9 PSSEN ok
 *  10 BAR ok       11 cmd read       12 mem decode     13 mmio mapped
 *  14 caps read    15 caps valid     16 bus master     17 handoff done
 *  18 switchover done (post-handoff) 19 ports powered 20 rings done
 */
static void xhci_step(int n)
{
    klog("XHCI", "step %d", n);
}

/* Write a 64-bit bus address into a controller's split low/high pair. */
static void xhci_set_64(volatile u32 *low, volatile u32 *high, dma_addr_t a)
{
    *low  = (u32)a;
    *high = (u32)(a >> 32);
}

/* ------------------------------------------------------------------ *
 * Interrupt-driven completion (opt-in, see USB_USE_INTERRUPTS).
 *
 * With interrupts enabled the event ring is drained by the hard IRQ
 * handler instead of by the polling loop in xhci_event_wait().  The
 * waiter still observes completion via the ring state, but the DMA
 * event processing now happens on the controller's interrupt, which is
 * what lets the CPU sleep instead of spinning.
 * ------------------------------------------------------------------ */
#ifdef USB_USE_INTERRUPTS
static int g_xhci_intr_en;
static struct usb_xhci_s *g_xhci;

static void xhci_irq_handler(void *arg)
{
    struct usb_xhci_s *xhci = arg;

    /* Ack the controller: clear the event-intercept bit (RW1C) and the
     * ring-interrupt-pending bit in IMAN (RW1C). */
    xhci->op->usbsts = xhci->op->usbsts & XHCI_STS_EINT;
    xhci->ir->iman  |= XHCI_IMAN_IP;

    /* Drain whatever the hardware wrote.  Idempotent with the poller
     * because of the TRB cycle bit. */
    xhci_process_events(xhci);
}

static void xhci_intr_enable(struct usb_xhci_s *xhci, u8 irq)
{
    xhci->ir->iman |= XHCI_IMAN_IE;
    xhci->op->usbcmd |= XHCI_CMD_INTE;

    if (usb_irq_register(irq, xhci_irq_handler, xhci) != 0) {
        klog("XHCI", "could not register IRQ %d handler", irq);
        return;
    }
    idt_set_gate(0x20 + irq, usb_irq_stub(irq), 0x08, 0x8E);
    usb_irq_enable(irq);
    g_xhci = xhci;
    g_xhci_intr_en = 1;
    klog("XHCI", "interrupts enabled on IRQ %d", irq);
}
#endif

/*
 * Intel PCH xHCIs whose USB2 ports are switchable between EHCI and
 * xHCI (Panther/Lynx/Wildcat Point era).  If firmware left ports on
 * EHCI, the xHCI controller runs fine but every port reads empty -
 * exactly the "works in QEMU, 0 devices on real hardware" symptom.
 * Route all switchable ports to xHCI before touching the controller.
 */
static const struct {
    u16 devid;
    const char *name;
    int switchover;
} intel_xhci_ids[] = {
    { 0x1E31, "Panther Point",    1 },
    { 0x8C31, "Lynx Point",       1 },
    { 0x8C33, "Lynx Point",       1 },
    { 0x8D31, "Lynx Point-EX",    1 },
    { 0x9C31, "Lynx Point-LP",    1 },
    { 0x9C33, "Lynx Point-LP",    1 },
    { 0x9CB1, "Wildcat Point-LP", 1 },
    { 0x9CB2, "Wildcat Point-LP", 1 },
    /* modern controllers: no EHCI to fight with */
    { 0xA12F, "Sunrise Point-H",  0 },
    { 0x9D2F, "Sunrise Point-LP", 0 },
    { 0xA36D, "Cannon Lake/Comet Lake", 0 },
    { 0x34ED, "Ice Lake-LP",      0 },
    { 0xA0ED, "Tiger Lake-LP",    0 },
};

static void intel_usb_port_switchover(u8 bus, u8 dev, u8 func,
                                      u16 devid)
{
    for (u32 i = 0; i < sizeof(intel_xhci_ids) / sizeof(intel_xhci_ids[0]);
         i++) {
        if (intel_xhci_ids[i].devid != devid)
            continue;
        klog("XHCI", "Intel %s xHCI (%04x:%04x)",
             intel_xhci_ids[i].name, PCI_VENDOR_INTEL, devid);
        if (!intel_xhci_ids[i].switchover)
            return;

#ifdef USB_DO_PORT_SWITCHOVER
        /*
         * Route every switchable USB2 port to xHCI.  XUSB2PR alone
         * moves the ports; PSSEN (speed-sense handoff to EHCI) is the
         * write this board's firmware reliably wedges on, so it is
         * opt-in and skipped by default.
         */
        pci_config_write(bus, dev, func, PCI_INTEL_XUSB2PR, 0x0000FFFF);
        u32 chk = pci_config_read(bus, dev, func, PCI_INTEL_XUSB2PR);
        klog("XHCI", "XUSB2PR written, readback=%08x", chk);
#ifdef USB_DO_PSSEN
        pci_config_write(bus, dev, func, PCI_INTEL_PSSEN, 0x00000000);
        klog("XHCI", "PSSEN cleared");
#else
        klog("XHCI", "skipping PSSEN (firmware wedges on it)");
#endif
        xhci_step(8);
        klog("XHCI", "USB2 ports routed to xHCI (XUSB2PR=%04x)",
             chk & 0xFFFF);
#else
        /* Firmware keeps the ports on its own EHCI controllers; the
         * EHCI driver owns them from there.  Contesting routing has
         * hard-frozen every board tested. */
        klog("XHCI", "port routing left to firmware");
        return;
#endif
        return;
    }
}

/*
 * Wake the function to D0 via its Power Management capability.
 * Firmware parks unused controllers in D3; MMIO access to a D3 PCH
 * internal device is undefined and can freeze the whole board.
 */
static void pci_set_d0(u8 bus, u8 dev, u8 func)
{
    xhci_step(2);
    u8 cap_ptr = pci_config_read_byte(bus, dev, func, PCI_CAPABILITIES);
    for (int guard = 0; guard < 16 && cap_ptr >= 0x40; guard++) {
        u8 cap_id = pci_config_read_byte(bus, dev, func, cap_ptr);
        if (cap_id == 0x01) {
            /* PMCSR: bits 1:0 = power state */
            xhci_step(3);
            u32 pmcsr = pci_config_read(bus, dev, func, cap_ptr + 2
                                        & 0xFC);
            u8 state = (pmcsr >> ((cap_ptr + 2 & 3) * 8)) & 0xFF;
            klog("XHCI", "pm cap at %02x, power state D%d",
                 cap_ptr, state & 0x03);
            if ((state & 0x03) != 0) {
                u32 aligned = pci_config_read(bus, dev, func,
                                              cap_ptr + 2 & 0xFC);
                u32 shift = ((cap_ptr + 2) & 3) * 8;
                aligned &= ~(0x03 << shift);
                pci_config_write(bus, dev, func, cap_ptr + 2 & 0xFC,
                                 aligned);
                xhci_step(4);
                usb_msleep(20);     /* D3 -> D0 transition time */
                xhci_step(5);
                klog("XHCI", "woke controller from D%d to D0",
                     state & 0x03);
            }
            return;
        }
        cap_ptr = pci_config_read_byte(bus, dev, func, cap_ptr + 1);
    }
}

// Diagnostic: show whatever serial-bus controllers ARE present.
static void pci_dump_serial_bus(void)
{
    for (int i = 0; i < pci_device_count(); i++) {
        struct pci_device *pd = pci_get_device(i);
        if (pd->class_code != 0x0C || pd->subclass != 0x03)
            continue;
        const char *kind = pd->progif == 0x30 ? "xHCI"
                         : pd->progif == 0x20 ? "EHCI"
                         : pd->progif == 0x10 ? "OHCI"
                         : pd->progif == 0x00 ? "UHCI"
                         : "other";
        klog("XHCI", "hint: %s controller at %d:%d.%d (%04x:%04x)",
             kind, pd->bus, pd->dev, pd->func,
             pd->vendor_id, pd->device_id);
    }
}

// Max time to wait for port power to stabilize and links to report a
// device.  Real hardware needs up to ~100ms USB2 debounce plus USB3 link
// training; QEMU reports CCS instantly.
#define XHCI_TIME_POSTPOWER 600
#define XHCI_PAGE_SIZE      4096

// A transfer/event ring pointer -> owning ring struct (ring is 256 aligned).
#define XHCI_RING(_trb) \
    ((struct xhci_ring *)((u32)(_trb) & ~(XHCI_RING_SIZE - 1)))

static const int speed_from_xhci[16] = {
    [0] = -1,
    [1] = USB_FULLSPEED,
    [2] = USB_LOWSPEED,
    [3] = USB_HIGHSPEED,
    [4] = USB_SUPERSPEED,
    [5 ... 15] = -1,
};

static const int speed_to_xhci[] = {
    [USB_FULLSPEED]  = 1,
    [USB_LOWSPEED]   = 2,
    [USB_HIGHSPEED]  = 3,
    [USB_SUPERSPEED] = 4,
};

static int wait_bit(volatile u32 *reg, u32 mask, int value, u32 timeout)
{
    u32 end = usb_now_ms() + timeout;
    while ((*reg & mask) != (u32)value) {
        if (usb_now_ms() >= end) {
            klog("XHCI", "register timeout (mask 0x%x want %d)", mask, value);
            return -1;
        }
        usb_msleep(1);
    }
    return 0;
}

/****************************************************************
 * Root hub
 ****************************************************************/
static int xhci_hub_detect(struct usbhub_s *hub, u32 port)
{
    struct usb_xhci_s *xhci = container_of(hub->cntl, struct usb_xhci_s, usb);
    u32 portsc = xhci->pr[port].portsc;
    return (portsc & XHCI_PORTSC_CCS) ? 1 : 0;
}

// Reset device on port
static int xhci_hub_reset(struct usbhub_s *hub, u32 port)
{
    struct usb_xhci_s *xhci = container_of(hub->cntl, struct usb_xhci_s, usb);
    u32 portsc = xhci->pr[port].portsc;
    if (!(portsc & XHCI_PORTSC_CCS))
        // Device no longer connected?!
        return -1;

    switch (xhci_get_field(portsc, XHCI_PORTSC_PLS)) {
    case PLS_U0:
        // A USB3 port - controller automatically performs reset
        break;
    case PLS_POLLING:
        // A USB2 port - perform device reset
        xhci->pr[port].portsc = portsc | XHCI_PORTSC_PR;
        break;
    default:
        return -1;
    }

    // Wait for device to complete reset and be enabled
    u32 end = usb_now_ms() + 100;
    for (;;) {
        portsc = xhci->pr[port].portsc;
        if (!(portsc & XHCI_PORTSC_CCS))
            // Device disconnected during reset
            return -1;
        if (portsc & XHCI_PORTSC_PED)
            // Reset complete
            break;
        if (usb_now_ms() >= end) {
            klog("XHCI", "port %d reset timeout", port);
            return -1;
        }
        usb_msleep(1);
    }

    int rc = speed_from_xhci[xhci_get_field(portsc, XHCI_PORTSC_SPEED)];
    klog("XHCI", "port %d enabled, speed %d", port + 1, rc);
    return rc;
}

static void xhci_hub_disconnect(struct usbhub_s *hub, u32 port)
{
    (void)hub;
    (void)port;
    // XXX - should turn the port power off.
}

static struct usbhub_op_s xhci_hub_ops = {
    .detect = xhci_hub_detect,
    .reset = xhci_hub_reset,
    .disconnect = xhci_hub_disconnect,
};

// Show raw port state so stuck bring-up is diagnosable on real boards.
static void xhci_dump_ports(struct usb_xhci_s *xhci)
{
    for (u32 i = 0; i < xhci->ports; i++)
        klog("XHCI", "port %d: portsc=%08x", i + 1,
             xhci->pr[i].portsc);
}

// Find any devices connected to the root hub.
static int xhci_check_ports(struct usb_xhci_s *xhci)
{
    klog("XHCI", "scanning %d ports", xhci->ports);
    // Wait for port power to stabilize and links to settle.  Exit early
    // as soon as any port reports a device.
    u32 end = usb_now_ms() + XHCI_TIME_POSTPOWER;
    for (;;) {
        int found = 0;
        for (u32 i = 0; i < xhci->ports; i++)
            if (xhci->pr[i].portsc & XHCI_PORTSC_CCS)
                found++;
        if (found || usb_now_ms() >= end)
            break;
        usb_msleep(10);
    }

    struct usbhub_s hub;
    klibc.memset(&hub, 0, sizeof(hub));
    hub.cntl = &xhci->usb;
    hub.portcount = xhci->ports;
    hub.op = &xhci_hub_ops;
    usb_enumerate(&hub);

    if (!hub.devcount) {
        klog("XHCI", "no devices - dumping raw port state:");
        xhci_dump_ports(xhci);
    }
    return hub.devcount;
}

/****************************************************************
 * Setup
 ****************************************************************/
#define XHCI_XECP_LEGSUP      1
#define XHCI_LEGSUP_BIOS_OWN  (1 << 16)
#define XHCI_LEGSUP_OS_OWN    (1 << 24)

/*
 * Take controller ownership from the BIOS/SMI handler (xHCI spec 7.1).
 * QEMU has no legacy support capability; most real firmware does, and
 * while it owns the controller ports may never report a device.
 */
static void xhci_bios_handoff(struct usb_xhci_s *xhci)
{
    u32 off = (xhci->caps->hccparams >> 16) & 0xffff;
    klog("XHCI", "handoff: hccparams=%08x xecp_off=%04x",
         xhci->caps->hccparams, off);
    /* offsets are in 32-bit words from BAR0; stay inside our 64K map */
    if (!off || off >= 0x4000) {
        klog("XHCI", "no extended caps (no BIOS handoff needed)");
        return;
    }

    volatile u32 *base = (volatile u32 *)(unsigned long)xhci->caps;
    volatile u32 *cap = base + off;
    for (int guard = 0; guard < 32; guard++) {
        u32 woff = (u32)(cap - base);
        if (woff >= 0x4000)
            break;              /* ran past the mapped region */

        u32 id = cap[0] & 0xff;
        u32 next = (cap[0] >> 16) & 0xffff;
        klog("XHCI", "xecp id=%u at %04x next=%u", id, woff, next);

        if (id == XHCI_XECP_LEGSUP) {
            // Claim ownership and wait for BIOS to let go.
            klog("XHCI", "writing OS-owned semaphore");
            cap[0] |= XHCI_LEGSUP_OS_OWN;
            klog("XHCI", "waiting for BIOS to release");
            if (wait_bit(&cap[0], XHCI_LEGSUP_BIOS_OWN, 0, 2000))
                klog("XHCI", "warning: BIOS kept xHCI ownership");
            else
                klog("XHCI", "BIOS handoff complete");
            // Silence firmware SMI sources.
            cap[1] = 0;
            klog("XHCI", "SMI enables cleared");
            return;
        }
        if (!next)
            break;
        cap += next;
    }
    klog("XHCI", "no legacy support capability found");
}

/*
 * Disarm USB legacy SMIs at the root clock generator.  This board's
 * firmware guards its device MMIO so aggressively that even READING
 * the EHCI legacy block freezes it - but the PCH PMC exposes SMI_EN
 * as plain I/O ports at ACPI PMBASE+0x30, with dedicated bits for
 * the legacy USB handlers:
 *   bit 13 LEGACY_USB_EN, bit 14 LEGACY_USB2_EN
 * PMBASE derives from FADT: PM1a_CNT sits at PMBASE+4 on Intel.
 * Only the USB sources are cleared - global SMI stays enabled.
 */
static void pch_disable_usb_smi(void)
{
    /* The FADT's pm1a_cnt_blk can be stale on this board - trust the
     * LPC bridge (D31:F0) instead: PMBASE @0x40, enable bit in
     * ACPI_CNTL @0x44 bit0. */
    u32 lpc_raw = pci_config_read(0, 31, 0, 0x40);
    u16 lpc_base = (u16)(lpc_raw & 0xFF80);
    u32 cntl = pci_config_read(0, 31, 0, 0x44);

    u32 fadt_cnt = acpi_fadt ? acpi_fadt->pm1a_cnt_blk : 0;
    u16 fadt_base = fadt_cnt ? (u16)(fadt_cnt - 0x04) : 0;

    klog("XHCI", "lpc pmbase raw=%08x -> %04x, cntl=%08x | fadt=%04x",
         lpc_raw, lpc_base, cntl, fadt_base);

    u16 pmbase;
    int have_lpc = (lpc_raw != 0xFFFFFFFF && lpc_base != 0);

    if (!have_lpc) {
        /* no LPC bridge visible (e.g. QEMU): trust FADT instead */
        klog("XHCI", "LPC PMBASE unreadable - using fadt=%04x",
             fadt_base);
        pmbase = fadt_base;
    } else {
        if (!(cntl & 1)) {
            klog("XHCI", "acpi io decode disabled - enabling");
            pci_config_write(0, 31, 0, 0x44, cntl | 0x00000001);
        }
        pmbase = lpc_base;
    }
    if (!pmbase) {
        klog("XHCI", "no usable PMBASE - cannot disarm usb smis");
        return;
    }

    klog("XHCI", "reading smi_en");
    u32 en = inl(pmbase + 0x30);
    u32 usb_bits = (u32)((1 << 13) | (1 << 14));   /* LEGACY_USB{,2}_EN */
    klog("XHCI", "smi_en=%08x", en);

    if (!(en & usb_bits)) {
        /* nothing to disarm - and on some firmwares writing this PMC
         * register at all wedges the box, so leave it alone */
        klog("XHCI", "usb legacy smis already clear - not writing");
        return;
    }

    en &= ~usb_bits;
    klog("XHCI", "writing smi_en=%08x", en);
    outl(pmbase + 0x30, en);

    klog("XHCI", "smi_en readback");
    u32 rb = inl(pmbase + 0x30);
    klog("XHCI", "smi_en after=%08x%s", rb,
         (rb & ((1 << 13) | (1 << 14))) ? " USB-BITS-STILL-SET" : "");

    u32 sts = inl(pmbase + 0x34);
    klog("XHCI", "smi_sts=%08x", sts);
    if (sts & ((1 << 13) | (1 << 14)))
        outl(pmbase + 0x34, sts & ((1 << 13) | (1 << 14)));
}

#ifdef USB_EHCI_DISARM
/*
 * Direct EHCI legacy-block takeover.  Opt-in only (-DUSB_EHCI_DISARM):
 * on boards whose firmware guards device MMIO this wedges even on a
 * plain register READ.
 */
static void ehci_disarm_legacy(void)
{
#ifdef USB_NO_EHCI_DISARM
    klog("XHCI", "skipping EHCI legacy disarm");
#else
    for (int i = 0; i < pci_device_count(); i++) {
        struct pci_device *pd = pci_get_device(i);
        if (!pd || !pd->present)
            continue;
        if (pd->class_code != 0x0C || pd->subclass != 0x03
            || pd->progif != 0x20)
            continue;

        u32 bar = pd->bar[0] & 0xFFFFFFF0;
        klog("XHCI", "ehci %02x:%02x.%d (%04x:%04x) bar=%08x",
             pd->bus, pd->dev, pd->func,
             pd->vendor_id, pd->device_id, bar);
        if (!bar)
            continue;

        /* same bring-up ritual as the xHCI side: D0 wake, then memory
         * decode - touching MMIO on a parked/disabled function is what
         * wedges boards */
        pci_set_d0(pd->bus, pd->dev, pd->func);
        u32 cmd = pci_config_read(pd->bus, pd->dev, pd->func,
                                  PCI_COMMAND);
        pci_config_write(pd->bus, pd->dev, pd->func, PCI_COMMAND,
                         (cmd & 0xFFFF) | 0x0002);
        klog("XHCI", "ehci: d0 + mem decode done");

        paging_map_region(bar, bar, 0x1000,
                          PAGE_PRESENT | PAGE_WRITE | PAGE_PWT | PAGE_PCD);
        klog("XHCI", "ehci: mapped");

        volatile u32 *legsup = (volatile u32 *)
            ((unsigned long)bar + 0x60);
        volatile u32 *ctlsts = (volatile u32 *)
            ((unsigned long)bar + 0x64);
        u32 ls = *legsup;
        u32 cs = *ctlsts;
        klog("XHCI", "ehci legsup=%08x ctlsts=%08x", ls, cs);
        if (!ls || ls == 0xFFFFFFFF)
            continue;               /* no legacy block implemented */

        /* silence SMI sources first, then claim ownership */
        *ctlsts = 0;
        klog("XHCI", "ehci: smi sources cleared");
        *legsup |= (1 << 24);
        klog("XHCI", "ehci: os-owned set");
        if (wait_bit(legsup, (1 << 16), 0, 500))
            klog("XHCI", "ehci: BIOS kept ownership");
        else
            klog("XHCI", "ehci: legacy disarmed");
    }
#endif
}
#endif /* USB_EHCI_DISARM */

static void configure_xhci(struct usb_xhci_s *xhci)
{
    u32 reg;

    xhci->devs = usb_dma_alloc(sizeof(*xhci->devs) * (xhci->slots + 1));
    xhci->eseg = usb_dma_alloc(sizeof(*xhci->eseg));
    xhci->cmds = usb_dma_alloc(sizeof(*xhci->cmds));
    xhci->evts = usb_dma_alloc(sizeof(*xhci->evts));
    if (!xhci->devs || !xhci->cmds || !xhci->evts || !xhci->eseg) {
        klog("XHCI", "no memory for controller structures");
        return;
    }

    reg = xhci->op->usbcmd;
    klog("XHCI", "usbcmd=%08x, halting controller", reg);
    if (reg & XHCI_CMD_RS) {
        reg &= ~XHCI_CMD_RS;
        xhci->op->usbcmd = reg;
        if (wait_bit(&xhci->op->usbsts, XHCI_STS_HCH, XHCI_STS_HCH, 32) != 0) {
            klog("XHCI", "failed to stop controller");
            return;
        }
    }

    xhci->op->usbcmd = XHCI_CMD_HCRST;
    if (wait_bit(&xhci->op->usbcmd, XHCI_CMD_HCRST, 0, 1000) != 0) {
        klog("XHCI", "HCRST timed out");
        return;
    }
    if (wait_bit(&xhci->op->usbsts, XHCI_STS_CNR, 0, 1000) != 0) {
        klog("XHCI", "CNR never cleared after reset");
        return;
    }

    // Power the ports immediately so an attached keyboard regains
    // power even if a later bring-up stage fails.  HCRST resets PP to
    // its default (off on most real controllers); qemu-xhci ignores
    // PP entirely.  The write is ignored when PPC is unsupported.
    for (u32 i = 0; i < xhci->ports; i++)
        xhci->pr[i].portsc = XHCI_PORTSC_PP;
    usb_msleep(XHCI_TIME_POSTPOWER / 10);

    klog("XHCI", "controller reset, programming rings");

    xhci->op->config = xhci->slots;
    xhci_set_64(&xhci->op->dcbaap_low, &xhci->op->dcbaap_high,
                usb_dma_to_bus(xhci->devs));
    xhci_set_64(&xhci->op->crcr_low, &xhci->op->crcr_high,
                usb_dma_to_bus(xhci->cmds) | 1);
    xhci->cmds->cs = 1;
    usb_dma_flush(xhci->cmds->ring, XHCI_RING_SIZE);

    xhci->eseg->ptr_low = (u32)usb_dma_to_bus(xhci->evts);
    xhci->eseg->ptr_high = (u32)(usb_dma_to_bus(xhci->evts) >> 32);
    xhci->eseg->size = XHCI_RING_ITEMS;
    usb_dma_flush(xhci->eseg, sizeof(*xhci->eseg));
    xhci->evts->cs = 1;
    usb_dma_flush(xhci->evts->ring, XHCI_RING_SIZE);

    xhci->ir->erstsz = 1;
    xhci_set_64(&xhci->ir->erstba_low, &xhci->ir->erstba_high,
                usb_dma_to_bus(xhci->eseg));
    xhci_set_64(&xhci->ir->erdp_low, &xhci->ir->erdp_high,
                usb_dma_to_bus(xhci->evts));

    reg = xhci->caps->hcsparams2;
    u32 spb = (reg >> 21 & 0x1f) << 5 | reg >> 27;
    if (spb > 1024) {
        /* garbage readback means the BAR is not what we think it is */
        klog("XHCI", "implausible scratchpad count %d - aborting", spb);
        return;
    }
    if (spb) {
        u32 *spba = usb_dma_alloc(sizeof(u32) * spb);
        void *pad = usb_dma_alloc_aligned(XHCI_PAGE_SIZE * spb, XHCI_PAGE_SIZE);
        if (!spba || !pad) {
            klog("XHCI", "no memory for scratchpad buffers");
            return;
        }
        for (int i = 0; i < (int)spb; i++)
            spba[i] = (u32)usb_dma_to_bus(pad) + (i * XHCI_PAGE_SIZE);
        usb_dma_flush(spba, sizeof(u32) * spb);
        xhci_set_64(&xhci->devs[0].ptr_low, &xhci->devs[0].ptr_high,
                    usb_dma_to_bus(spba));
    }
    usb_dma_flush(xhci->devs, sizeof(*xhci->devs) * (xhci->slots + 1));

    klog("XHCI", "starting controller");
    reg = xhci->op->usbcmd;
    reg |= XHCI_CMD_RS;
    xhci->op->usbcmd = reg;
    if (wait_bit(&xhci->op->usbsts, XHCI_STS_HCH, 0, 100) != 0) {
        klog("XHCI", "controller failed to start (HCH stuck)");
        return;
    }
    klog("XHCI", "controller running, %d ports %d slots",
         xhci->ports, xhci->slots);

    // Find devices
    int count = xhci_check_ports(xhci);
    if (count == 0)
        klog("XHCI", "enumration failed");
    else
        klog("XHCI", "enumeration done, devices=%d", count);
  }

int xhci_setup(void)
{
    u8 bus = 0, dev = 0, func = 0;

    // 1. strict class match (0C/03/30)
    int found = pci_find_class(XHCI_PCI_CLASS, XHCI_PCI_SUBCLASS,
                               XHCI_PCI_PROGIF, &bus, &dev, &func);
    // 2. any serial-USB progif (some firmware mislabels progif)
    if (found != 0) {
        found = pci_find_class(XHCI_PCI_CLASS, XHCI_PCI_SUBCLASS,
                               0xFF, &bus, &dev, &func);
        if (found == 0)
            klog("XHCI", "found xHCI with unusual progif 0x%02x",
                 pci_config_read(bus, dev, func, PCI_PROGIF) >> 8
                     & 0xff);
    }
    // 3. known Intel vendor/device IDs (class codes can be hidden)
    if (found != 0) {
        for (u32 i = 0;
             i < sizeof(intel_xhci_ids) / sizeof(intel_xhci_ids[0]); i++) {
            struct pci_device *pd =
                pci_find_vendev(PCI_VENDOR_INTEL, intel_xhci_ids[i].devid);
            if (!pd)
                continue;
            bus = pd->bus;
            dev = pd->dev;
            func = pd->func;
            found = 0;
            klog("XHCI", "matched Intel %s by device ID",
                 intel_xhci_ids[i].name);
            break;
        }
    }
    if (found != 0) {
        klog("XHCI", "no xHCI controller found");
        pci_dump_serial_bus();
        return -1;
    }

    u32 dv = pci_config_read(bus, dev, func, PCI_VENDOR_ID);
    u16 ven = dv & 0xFFFF;
    u16 devid = dv >> 16;
    klog("XHCI", "probing %04x:%04x at %d:%d.%d", ven, devid,
         bus, dev, func);
    xhci_step(1);

    // Firmware may have parked the controller in D3 - wake it first.
    pci_set_d0(bus, dev, func);
    xhci_step(6);

    u32 bar = pci_config_read(bus, dev, func, PCI_BAR0);
    bar &= 0xFFFFFFF0;

    // 64-bit BAR: we identity-map with 32-bit math only.
    u32 bar_high = pci_config_read(bus, dev, func, PCI_BAR0 + 4);
    if (bar_high) {
        klog("XHCI", "BAR0 above 4GB (%08x%08x) unsupported",
             bar_high, bar);
        return -1;
    }
    xhci_step(10);

    // Enable memory decode ONLY (SeaBIOS-style).  Bus mastering is
    // deferred until the register readback proves sane - flipping on
    // DMA access mid-handoff makes some firmware SMI handlers act up.
    // Upper 16 bits (status) are written as zero: status bits are
    // RW1C, echoing stale errors back would ack/clear them.
    u32 cmd = pci_config_read(bus, dev, func, PCI_COMMAND);
    klog("XHCI", "pci command=%08x", cmd);
    xhci_step(11);
    klog("XHCI", "enabling mem decode");
    pci_config_write(bus, dev, func, PCI_COMMAND,
                     (cmd & 0xFFFF) | 0x0002);
    xhci_step(12);

    klog("XHCI", "mapping mmio at %08x", bar);
    // PWT|PCD => strongly uncacheable.  Mapping device registers as
    // default-WB lets the CPU speculatively prefetch from them, which
    // wedges real chipsets (Lynx Point) even though qemu tolerates it.
    paging_map_region(bar, bar, 0x10000,
                      PAGE_PRESENT | PAGE_WRITE | PAGE_PWT | PAGE_PCD);
    klog("XHCI", "mmio mapped");
    xhci_step(13);

    struct usb_xhci_s *xhci = usb_alloc(sizeof(*xhci));
    if (!xhci) {
        klog("XHCI", "no memory for controller state");
        return -1;
    }

    volatile struct xhci_caps *caps = (void *)(unsigned long)bar;

    // First MMIO touch: one byte, immediately logged.  If the board
    // dies on this read the log stops right here.
    u32 caplength = caps->caplength;
    klog("XHCI", "caplength=%02x", caplength);
    xhci_step(14);

    /*
     * Validate the register readback BEFORE deriving anything from it.
     * A dead/unassigned BAR reads as all-1s (or 0) and blindly using
     * those values means writing random MMIO - which hard-hangs some
     * boards instead of faulting cleanly.
     */
    u32 hciver    = caps->hciversion;
    u32 hcs1      = caps->hcsparams1;
    u32 hcc       = caps->hccparams;
    u32 dboff     = caps->dboff & ~3u;
    u32 rtsoff    = caps->rtsoff & ~3u;

    if (!bar || !caplength || caplength == 0xFF || hciver == 0xFFFF
        || !hcs1 || hcs1 == 0xFFFFFFFF) {
        klog("XHCI", "caps readback invalid "
             "(caplen=%02x ver=%04x hcs1=%08x) - BAR unusable",
             caplength, hciver, hcs1);
        return -1;
    }

    xhci->ports = (hcs1 >> 24) & 0xff;
    xhci->slots = hcs1 & 0xff;
    xhci->context64 = (hcc & 0x04) ? 1 : 0;
    xhci->usb.type = USB_TYPE_XHCI;

    if (!xhci->ports || !xhci->slots
        || xhci->ports > 128 || xhci->slots > 128) {
        klog("XHCI", "implausible ports/slots (%d/%d)",
             xhci->ports, xhci->slots);
        return -1;
    }
    if (dboff < 0x100 || rtsoff < 0x100
        || dboff >= 0x10000 || rtsoff >= 0x10000) {
        klog("XHCI", "implausible dboff/rtsoff (%08x/%08x)",
             dboff, rtsoff);
        return -1;
    }

    xhci->caps = caps;
    xhci->op = (void *)((unsigned long)caps + caplength);
    xhci->pr = (void *)((unsigned long)caps + caplength + 0x400);
    xhci->db = (void *)((unsigned long)caps + dboff);
    xhci->ir = (void *)((unsigned long)caps + rtsoff + 0x20);

    // Registers read back sane - now allow DMA.
    klog("XHCI", "enabling bus master");
    pci_config_write(bus, dev, func, PCI_COMMAND,
                     (cmd & 0xFFFF) | 0x0006);
    xhci_step(16);

    klog("XHCI", "controller %04x:%04x at PCI %d:%d.%d BAR0 0x%x (%d ports, %d slots, %d-byte ctx)",
         ven, devid, bus, dev, func, bar, xhci->ports, xhci->slots,
         xhci->context64 ? 64 : 32);

    klog("XHCI", "claiming ownership");
    xhci_bios_handoff(xhci);
    xhci_step(17);

    // Silence the firmware's legacy-USB SMI handlers via the PCH PMC.
    // Opt-in (-DUSB_PMC_SMI_DISARM): on boards whose firmware guards
    // these registers the write wedges, and boards that arm legacy
    // USB SMIs are vanishingly rare.
#ifdef USB_PMC_SMI_DISARM
    pch_disable_usb_smi();
#endif
#ifdef USB_EHCI_DISARM
    ehci_disarm_legacy();
#endif

    // Intel PCHs: route the ports to us.  Only AFTER the handoff -
    // firmware USB SMIs must be silenced before these writes, or the
    // SMI handler races us and freezes the board.
    if (ven == PCI_VENDOR_INTEL)
        intel_usb_port_switchover(bus, dev, func, devid);
    xhci_step(18);

    configure_xhci(xhci);

#ifdef USB_USE_INTERRUPTS
    /* Route the controller's PCI interrupt line to our dispatcher and
     * let the hardware assert it.  The poll path keeps working as a
     * fallback: xhci_event_wait() sees the same ring state the ISR
     * updates, so completion is observed either way. */
    u8 irq = pci_config_read(bus, dev, func, PCI_INTERRUPT_LINE) & 0xff;
    if (irq)
        xhci_intr_enable(xhci, irq);
    else
        klog("XHCI", "no PCI interrupt line - staying poll-based");
#endif

    return 0;
}

/****************************************************************
 * End point communication
 ****************************************************************/
// Signal the hardware to process events on a TRB ring
static void xhci_doorbell(struct usb_xhci_s *xhci, u32 slotid, u32 value)
{
    /* Order every TRB/context store the device must see ahead of the
     * doorbell write.  Without this fence a 32-bit controller can read
     * stale ring data off the bus. */
    usb_dma_wmb();
    xhci->db[slotid].doorbell = value;
}

// Dequeue events on the XHCI command ring generated by the hardware
static void xhci_process_events(struct usb_xhci_s *xhci)
{
    struct xhci_ring *evts = xhci->evts;

    for (;;) {
        /* check for event */
        u32 nidx = evts->nidx;
        u32 cs = evts->cs;
        struct xhci_trb *etrb = evts->ring + nidx;
        /* The device wrote this TRB straight to memory; drop any stale
         * CPU cache line so we read what the controller actually put
         * there (the cycle bit in particular must be fresh). */
        usb_dma_invalidate(etrb, sizeof(*etrb));
        u32 control = etrb->control;
        if ((control & TRB_C) != (cs ? 1 : 0))
            return;

        /* process event */
        u32 evt_type = TRB_TYPE(control);
        u32 evt_cc = (etrb->status >> 24) & 0xff;
        switch (evt_type) {
        case ER_TRANSFER:
        case ER_COMMAND_COMPLETE:
        {
            struct xhci_trb  *rtrb = (void *)etrb->ptr_low;
            struct xhci_ring *ring = XHCI_RING(rtrb);
            struct xhci_trb  *evt = &ring->evt;
            u32 eidx = rtrb - ring->ring + 1;
            klibc.memcpy(evt, etrb, sizeof(*etrb));
            ring->eidx = eidx;
            break;
        }
        case ER_PORT_STATUS_CHANGE:
        {
            u32 port = ((etrb->ptr_low >> 24) & 0xff) - 1;
            // Read status, and clear port status change bits
            u32 portsc = xhci->pr[port].portsc;
            u32 pclear = (((portsc & ~(XHCI_PORTSC_PED | XHCI_PORTSC_PR))
                           & ~(XHCI_PORTSC_PLS_MASK << XHCI_PORTSC_PLS_SHIFT))
                          | (1 << XHCI_PORTSC_PLS_SHIFT));
            xhci->pr[port].portsc = pclear;
            break;
        }
        default:
            klog("XHCI", "unknown event, type %d, cc %d", evt_type, evt_cc);
            break;
        }

        /* move ring index, notify xhci */
        nidx++;
        if (nidx == XHCI_RING_ITEMS) {
            nidx = 0;
            cs = cs ? 0 : 1;
            evts->cs = cs;
        }
        evts->nidx = nidx;
        dma_addr_t erdp = usb_dma_to_bus(evts->ring + nidx) | XHCI_IR_EHB;
        xhci_set_64(&xhci->ir->erdp_low, &xhci->ir->erdp_high, erdp);
    }
}

// Check if a ring has any pending TRBs
static int xhci_ring_busy(struct xhci_ring *ring)
{
    u32 eidx = ring->eidx;
    u32 nidx = ring->nidx;
    return (eidx != nidx);
}

// Wait for a ring to empty (all TRBs processed by hardware)
static int xhci_event_wait(struct usb_xhci_s *xhci,
                           struct xhci_ring *ring,
                           u32 timeout)
{
    u32 end = usb_now_ms() + timeout;

    for (;;) {
        xhci_process_events(xhci);
        if (!xhci_ring_busy(ring)) {
            u32 status = ring->evt.status;
            return (status >> 24) & 0xff;
        }
        if (usb_now_ms() >= end) {
            klog("XHCI", "event wait timeout");
            return -1;
        }
        usb_msleep(1);
    }
}

// Add a TRB to the given ring
static void xhci_trb_fill(struct xhci_ring *ring,
                          void *data, u32 xferlen, u32 flags)
{
    struct xhci_trb *dst = &ring->ring[ring->nidx];
    if (flags & TRB_TR_IDT) {
        klibc.memcpy(&dst->ptr_low, data, xferlen);
    } else {
        dst->ptr_low = (u32)data;
        dst->ptr_high = 0;
    }
    dst->status = xferlen;
    dst->control = flags | (ring->cs ? TRB_C : 0);
}

// Queue a TRB onto a ring, wrapping ring as needed
static void xhci_trb_queue(struct xhci_ring *ring,
                           void *data, u32 xferlen, u32 flags)
{
    if (ring->nidx >= XHCI_RING_ITEMS - 1) {
        xhci_trb_fill(ring, ring->ring, 0, (TR_LINK << 10) | TRB_LK_TC);
        ring->nidx = 0;
        ring->cs ^= 1;
    }

    xhci_trb_fill(ring, data, xferlen, flags);
    ring->nidx++;
    usb_dma_flush(ring->ring, XHCI_RING_SIZE);
}

// Submit a command to the xhci controller ring
static int xhci_cmd_submit(struct usb_xhci_s *xhci, struct xhci_inctx *inctx,
                           u32 flags)
{
    if (inctx) {
        struct xhci_slotctx *slot = (void *)&inctx[1 << xhci->context64];
        u32 port = ((slot->ctx[1] >> 16) & 0xff) - 1;
        u32 portsc = xhci->pr[port].portsc;
        if (!(portsc & XHCI_PORTSC_CCS)) {
            // Device no longer connected?!
            klog("XHCI", "device on port %d disconnected during command",
                 port + 1);
            return -1;
        }
        u32 size = (sizeof(struct xhci_inctx) * 33) << xhci->context64;
        usb_dma_flush(inctx, size);
    }

    xhci_trb_queue(xhci->cmds, inctx, 0, flags);
    xhci_doorbell(xhci, 0, 0);
    int rc = xhci_event_wait(xhci, xhci->cmds, 1000);
    return rc;
}

static int xhci_cmd_enable_slot(struct usb_xhci_s *xhci)
{
    int cc = xhci_cmd_submit(xhci, NULL, CR_ENABLE_SLOT << 10);
    if (cc != CC_SUCCESS)
        return -1;
    return (xhci->cmds->evt.control >> 24) & 0xff;
}

static int xhci_cmd_disable_slot(struct usb_xhci_s *xhci, u32 slotid)
{
    return xhci_cmd_submit(xhci, NULL,
                           (CR_DISABLE_SLOT << 10) | (slotid << 24));
}

static int xhci_cmd_address_device(struct usb_xhci_s *xhci, u32 slotid,
                                   struct xhci_inctx *inctx)
{
    return xhci_cmd_submit(xhci, inctx,
                           (CR_ADDRESS_DEVICE << 10) | (slotid << 24));
}

static int xhci_cmd_configure_endpoint(struct usb_xhci_s *xhci, u32 slotid,
                                       struct xhci_inctx *inctx)
{
    return xhci_cmd_submit(xhci, inctx,
                           (CR_CONFIGURE_ENDPOINT << 10) | (slotid << 24));
}

static int xhci_cmd_evaluate_context(struct usb_xhci_s *xhci, u32 slotid,
                                     struct xhci_inctx *inctx)
{
    return xhci_cmd_submit(xhci, inctx,
                           (CR_EVALUATE_CONTEXT << 10) | (slotid << 24));
}

static struct xhci_inctx *xhci_alloc_inctx(struct usbdevice_s *usbdev,
                                           int maxepid)
{
    struct usb_xhci_s *xhci = container_of(usbdev->hub->cntl,
                                           struct usb_xhci_s, usb);
    int size = (sizeof(struct xhci_inctx) * 33) << xhci->context64;
    struct xhci_inctx *in = usb_dma_alloc(size);
    if (!in)
        return NULL;

    struct xhci_slotctx *slot = (void *)&in[1 << xhci->context64];
    slot->ctx[0] |= maxepid << 27; // context entries
    slot->ctx[0] |= speed_to_xhci[usbdev->speed] << 20;
    slot->ctx[1] |= (usbdev->port + 1) << 16;

    return in;
}

static struct usb_pipe *xhci_alloc_pipe(struct usbdevice_s *usbdev,
                                        struct usb_endpoint_descriptor *epdesc)
{
    u8 eptype = epdesc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
    struct usb_xhci_s *xhci = container_of(usbdev->hub->cntl,
                                           struct usb_xhci_s, usb);
    struct xhci_pipe *pipe;
    u32 epid;

    if (epdesc->bEndpointAddress == 0) {
        epid = 1;
    } else {
        epid = (epdesc->bEndpointAddress & 0x0f) * 2;
        epid += (epdesc->bEndpointAddress & USB_DIR_IN) ? 1 : 0;
    }

    pipe = usb_dma_alloc(sizeof(*pipe));
    if (!pipe)
        return NULL;

    usb_desc2pipe(&pipe->pipe, usbdev, epdesc);
    pipe->epid = epid;
    pipe->reqs.cs = 1;
    if (eptype == USB_ENDPOINT_XFER_INT) {
        pipe->buf = usb_dma_alloc(pipe->pipe.maxpacket);
        if (!pipe->buf)
            return NULL;
    }

    // Allocate input context and initialize endpoint info.
    struct xhci_inctx *in = xhci_alloc_inctx(usbdev, epid);
    if (!in)
        goto fail;
    in->add = 0x01 | (1 << epid);
    struct xhci_epctx *ep = (void *)&in[(pipe->epid + 1) << xhci->context64];
    if (eptype == USB_ENDPOINT_XFER_INT) {
        // xHCI 1.0+: Interval is bits 15:12 = log2(service interval in
        // microframes).  Some controllers refuse to schedule an interrupt
        // endpoint when Interval is 0, so always write a valid value.
        // Keep the value even and < 8 so the xHCI 0.96 (SeaBIOS/QEMU)
        // MaxPStreams (bits 12:10) and LSA (bit 15) fields stay clear,
        // and set CErr=3 at bits 17:16 for 1.0 controllers.
        u32 interval;
        if (usbdev->speed == USB_HIGHSPEED || usbdev->speed == USB_SUPERSPEED)
            interval = epdesc->bInterval;
        else
            interval = __fls(epdesc->bInterval) + 4; // log2(8 * bInterval)
        interval &= ~1;
        if (interval < 2)
            interval = 2;
        if (interval > 6)
            interval = 6;
        ep->ctx[0] = (interval << 12) | (3 << 16);
        klog("XHCI", "int ep epid=%d speed=%d bInterval=%d interval=%d ctx0=%x",
             pipe->epid, usbdev->speed, epdesc->bInterval, interval, ep->ctx[0]);
    }
    ep->ctx[1] |= eptype << 3;
    if (epdesc->bEndpointAddress & USB_DIR_IN
        || eptype == USB_ENDPOINT_XFER_CONTROL)
        ep->ctx[1] |= 1 << 5;
    ep->ctx[1] |= pipe->pipe.maxpacket << 16;
    xhci_set_64(&ep->deq_low, &ep->deq_high,
                usb_dma_to_bus(&pipe->reqs.ring[0]) | 1);   // dcs
    ep->length = pipe->pipe.maxpacket;

    if (pipe->epid == 1) {
        // Enable slot.
        u32 size = (sizeof(struct xhci_slotctx) * 32) << xhci->context64;
        struct xhci_slotctx *dev = usb_dma_alloc(size);
        if (!dev)
            goto fail;
        int slotid = xhci_cmd_enable_slot(xhci);
        if (slotid < 0) {
            klog("XHCI", "enable slot: failed");
            goto fail;
        }
        xhci_set_64(&xhci->devs[slotid].ptr_low, &xhci->devs[slotid].ptr_high,
                    usb_dma_to_bus(dev));
        usb_dma_flush(&xhci->devs[slotid], sizeof(xhci->devs[slotid]));

        // Send set_address command.
        int cc = xhci_cmd_address_device(xhci, slotid, in);
        if (cc != CC_SUCCESS) {
            klog("XHCI", "address device: failed (cc %d)", cc);
            cc = xhci_cmd_disable_slot(xhci, slotid);
            if (cc != CC_SUCCESS)
                klog("XHCI", "disable slot failed (cc %d)", cc);
            xhci->devs[slotid].ptr_low = 0;
            goto fail;
        }
        pipe->slotid = slotid;
        klog("XHCI", "device on port %d addressed (slot %d)",
             usbdev->port + 1, slotid);
    } else {
        struct xhci_pipe *defpipe = container_of(usbdev->defpipe,
                                                 struct xhci_pipe, pipe);
        pipe->slotid = defpipe->slotid;
        // Send configure command.
        int cc = xhci_cmd_configure_endpoint(xhci, pipe->slotid, in);
        if (cc != CC_SUCCESS) {
            klog("XHCI", "configure endpoint: failed (cc %d)", cc);
            goto fail;
        }
    }
    return &pipe->pipe;

fail:
    return NULL;
}

struct usb_pipe *xhci_realloc_pipe(struct usbdevice_s *usbdev,
                                   struct usb_pipe *upipe,
                                   struct usb_endpoint_descriptor *epdesc)
{
    if (!epdesc) {
        usb_add_freelist(upipe);
        return NULL;
    }
    if (!upipe)
        return xhci_alloc_pipe(usbdev, epdesc);
    u8 eptype = epdesc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
    int oldmaxpacket = upipe->maxpacket;
    usb_desc2pipe(upipe, usbdev, epdesc);
    struct xhci_pipe *pipe = container_of(upipe, struct xhci_pipe, pipe);
    struct usb_xhci_s *xhci = container_of(pipe->pipe.cntl,
                                           struct usb_xhci_s, usb);
    if (eptype != USB_ENDPOINT_XFER_CONTROL
        || upipe->maxpacket == oldmaxpacket)
        return upipe;

    // maxpacket has changed on control endpoint - update controller.
    klog("XHCI", "reconf ctl endpoint pkt size: %d -> %d",
         oldmaxpacket, pipe->pipe.maxpacket);
    struct xhci_inctx *in = xhci_alloc_inctx(usbdev, 1);
    if (!in)
        return upipe;
    in->add = (1 << 1);
    struct xhci_epctx *ep = (void *)&in[2 << xhci->context64];
    ep->ctx[1] |= (pipe->pipe.maxpacket << 16);
    int cc = xhci_cmd_evaluate_context(xhci, pipe->slotid, in);
    if (cc != CC_SUCCESS)
        klog("XHCI", "reconf ctl endpoint: failed (cc %d)", cc);

    return upipe;
}

// Submit a USB "setup" message request to the pipe's ring
static void xhci_xfer_setup(struct xhci_pipe *pipe, int dir, void *cmd,
                            void *data, int datalen)
{
    struct usb_xhci_s *xhci = container_of(pipe->pipe.cntl,
                                           struct usb_xhci_s, usb);
    xhci_trb_queue(&pipe->reqs, cmd, USB_CONTROL_SETUP_SIZE,
                   (TR_SETUP << 10) | TRB_TR_IDT
                   | ((datalen ? (dir ? 3 : 2) : 0) << 16));
    if (datalen)
        xhci_trb_queue(&pipe->reqs, data, datalen, (TR_DATA << 10)
                       | ((dir ? 1 : 0) << 16));
    xhci_trb_queue(&pipe->reqs, NULL, 0, (TR_STATUS << 10) | TRB_TR_IOC
                   | ((dir ? 0 : 1) << 16));
    xhci_doorbell(xhci, pipe->slotid, pipe->epid);
}

// Submit a USB transfer request to the pipe's ring
static void xhci_xfer_normal(struct xhci_pipe *pipe,
                             void *data, int datalen)
{
    struct usb_xhci_s *xhci = container_of(pipe->pipe.cntl,
                                           struct usb_xhci_s, usb);
    xhci_trb_queue(&pipe->reqs, data, datalen, (TR_NORMAL << 10) | TRB_TR_IOC);
    xhci_doorbell(xhci, pipe->slotid, pipe->epid);
}

int xhci_send_pipe(struct usb_pipe *p, int dir, const void *cmd,
                   void *data, int datalen)
{
    struct xhci_pipe *pipe = container_of(p, struct xhci_pipe, pipe);
    struct usb_xhci_s *xhci = container_of(pipe->pipe.cntl,
                                           struct usb_xhci_s, usb);

    if (cmd) {
        const struct usb_ctrlrequest *req = cmd;
        if (req->bRequest == USB_REQ_SET_ADDRESS)
            // Set address command sent during xhci_alloc_pipe.
            return 0;
        xhci_xfer_setup(pipe, dir, (void *)req, data, datalen);
    } else {
        xhci_xfer_normal(pipe, data, datalen);
    }

    int cc = xhci_event_wait(xhci, &pipe->reqs, usb_xfer_time(p, datalen));
    if (cc != CC_SUCCESS) {
        klog("XHCI", "xfer failed (cc %d)", cc);
        return -1;
    }

    return 0;
}

int xhci_poll_intr(struct usb_pipe *p, void *data)
{
    struct xhci_pipe *pipe = container_of(p, struct xhci_pipe, pipe);
    struct usb_xhci_s *xhci = container_of(pipe->pipe.cntl,
                                           struct usb_xhci_s, usb);
    u32 len = pipe->pipe.maxpacket;
    void *buf = pipe->buf;
    int bufused = pipe->bufused;

    if (!bufused) {
        xhci_xfer_normal(pipe, buf, len);
        pipe->bufused = 1;
        return -1;
    }

    xhci_process_events(xhci);
    if (xhci_ring_busy(&pipe->reqs))
        return -1;
    usb_dma_invalidate(buf, len);
    klibc.memcpy(data, buf, len);
    xhci_xfer_normal(pipe, buf, len);
    return 0;
}
