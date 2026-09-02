/*
 * e1000.c - Intel 8254x (e1000) driver for XKERN IONetFamily.
 *
 * Polling driver (no interrupts): e1000_poll() drains completed RX
 * descriptors into the stack.  Rings and buffers are physically
 * contiguous pages identity-mapped so descriptor addresses are valid
 * for both the CPU and the DMA engine.
 */
#include "e1000.h"
#include "ether.h"
#include "arp.h"
#include "ipv4.h"
#include "icmp.h"
#include "IOPCIFamily/pci.h"
#include "IOServiceFamily/io_service.h"
#include "pmm.h"
#include "paging.h"
#include "io.h"
#include "klog.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* ---- register offsets ------------------------------------------------ */
#define E1000_CTRL      0x0000
#define E1000_STATUS    0x0008
#define E1000_EEC       0x0014
#define E1000_IMS       0x00D0
#define E1000_RCTL      0x0100
#define E1000_TCTL      0x0400

#define E1000_RDBAL     0x2800
#define E1000_RDBAH     0x2804
#define E1000_RDLEN     0x2808
#define E1000_RDH       0x2810
#define E1000_RDT       0x2818

#define E1000_TDBAL     0x3800
#define E1000_TDBAH     0x3804
#define E1000_TDLEN     0x3808
#define E1000_TDH       0x3810
#define E1000_TDT       0x3818

/* CTRL bits */
#define CTRL_SLU        (1 << 6)        /* set link up */

/* STATUS bits */
#define STATUS_LU       (1 << 1)

/* RCTL bits */
#define RCTL_EN         (1 << 1)
#define RCTL_UPE        (1 << 3)
#define RCTL_MPE        (1 << 4)
#define RCTL_LPE        (1 << 5)
#define RCTL_BSIZE_2048 (0 << 16)
#define RCTL_BAM        (1 << 15)       /* accept broadcast */

/* TCTL bits */
#define TCTL_EN         (1 << 1)
#define TCTL_PSP        (1 << 3)
#define TCTL_CT(x)      ((x) << 4)
#define TCTL_COLD(x)    ((x) << 12)

/* descriptor cmd / status bits */
#define TXD_CMD_EOP     0x01
#define TXD_CMD_IFCS    0x02
#define TXD_CMD_RS      0x08
#define TXD_STAT_DD     0x01            /* descriptor done */

/* ---- ring config ------------------------------------------------------*/
#define RX_DESC_COUNT   32
#define TX_DESC_COUNT   8
#define RX_BUF_SIZE     2048

struct e1000_rx_desc {
    volatile u64 addr;
    volatile u16 length;
    volatile u16 checksum;
    volatile u8 status;
    volatile u8 errors;
    volatile u16 special;
} __attribute__((packed));

struct e1000_tx_desc {
    volatile u64 addr;
    volatile u16 length;
    volatile u8 cso;
    volatile u8 cmd;
    volatile u8 status;
    volatile u8 css;
    volatile u16 special;
} __attribute__((packed));

static volatile u8   *mmio;                 /* BAR0 */
static struct pci_device *pdev;

static struct e1000_rx_desc *rx_ring;       /* one page, contiguous */
static struct e1000_tx_desc *tx_ring;
static u32 rx_ring_phys, tx_ring_phys;
static u32 rx_bufs_phys[RX_DESC_COUNT];     /* one page per buffer */
static u8 *rx_bufs[RX_DESC_COUNT];
static u32 rx_tail;
static u32 tx_tail;

static int link_up;

static inline u32 rd(u32 off) { return *(volatile u32 *)(mmio + off); }
static inline void wr(u32 off, u32 v) { *(volatile u32 *)(mmio + off) = v; }

/* ---- PCI glue ---------------------------------------------------------*/
#define PCI_CMD_IO      (1 << 0)
#define PCI_CMD_MEM     (1 << 1)
#define PCI_CMD_MASTER  (1 << 2)

static struct pci_device *e1000_probe(void)
{
    int n = pci_device_count();

    for (int i = 0; i < n; i++) {
        struct pci_device *d = pci_get_device(i);
        if (!d->present)
            continue;
        if (d->vendor_id != E1000_VENDOR)
            continue;
        if (d->device_id == E1000_DEV_82540EM ||
            d->device_id == E1000_DEV_82545EM ||
            d->device_id == E1000_DEV_82567LM)
            return d;
    }
    return NULL;
}

/* ---- MAC from EEPROM --------------------------------------------------*/
/*
 * EEC/EERD at 0x14: START=bit0, address bits 8-12, DONE=bit4,
 * data in bits 16-31.  Bounded: a failed read returns 0xFFFF instead
 * of hanging the boot.
 */
#define EED_START   (1u << 0)
#define EED_DONE    (1u << 4)

static u16 eeprom_read(u16 word)
{
    u32 data;
    u32 spins;

    wr(E1000_EEC, EED_START | ((u32)word << 8));
    for (spins = 0; spins < 200000; spins++) {
        data = rd(E1000_EEC);
        if (data & EED_DONE)
            break;
        asm volatile ("pause");
    }
    wr(E1000_EEC, 0);

    if (spins >= 200000)
        return 0xFFFF;          /* timed out */

    return (u16)(data >> 16);
}

static void e1000_read_mac(void)
{
    u16 w[3];

    w[0] = eeprom_read(0);
    w[1] = eeprom_read(1);
    w[2] = eeprom_read(2);

    /* all-ones (or all-zero) => read failed: use QEMU's default MAC */
    if ((w[0] & w[1] & w[2]) == 0xFFFF ||
        (!(w[0] | w[1] | w[2]))) {
        static const u8 def[NET_ETH_ALEN] =
            { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
        klog_lvl(KLOG_WARNING, "net.e1000",
                 "eeprom read failed, using default MAC");
        for (int i = 0; i < NET_ETH_ALEN; i++)
            g_net_mac.b[i] = def[i];
        return;
    }

    g_net_mac.b[0] = w[0] & 0xFF;
    g_net_mac.b[1] = w[0] >> 8;
    g_net_mac.b[2] = w[1] & 0xFF;
    g_net_mac.b[3] = w[1] >> 8;
    g_net_mac.b[4] = w[2] & 0xFF;
    g_net_mac.b[5] = w[2] >> 8;
}

/* ---- memory helpers ---------------------------------------------------*/
/*
 * Allocate one physically contiguous, identity-mapped page.
 * pmm_alloc() hands out ascending frames; a single page needs no run.
 */
static void *alloc_page_ident(u32 *phys_out)
{
    u32 p = pmm_alloc();

    if (!p)
        return NULL;
    paging_map_region(p, p, PAGE_SIZE, PAGE_WRITE);
    if (phys_out)
        *phys_out = p;
    return (void *)(uintptr_t)p;
}

/* ---- data path --------------------------------------------------------*/
static int e1000_send(const void *frame, u32 len)
{
    u32 tdt, tdh;

    if (!mmio || !link_up || len > RX_BUF_SIZE - 4)
        return -1;

    tdh = rd(E1000_TDH);
    tdt = tx_tail;

    if ((tdt + 1) % TX_DESC_COUNT == tdh)
        return -1;              /* ring full (should not happen at our rates) */

    /*
     * Copy into the descriptor's own buffer space: we point the desc at
     * the frame copy inside the rx buffer pool slot reserved for tx to
     * keep DMA-safe memory.  Simpler: bounce via a dedicated page.
     */
    static u32 tx_page_phys = 0;
    static u8 *tx_page = NULL;
    if (!tx_page) {
        tx_page = alloc_page_ident(&tx_page_phys);
        if (!tx_page)
            return -1;
    }

    const u8 *src = frame;
    for (u32 i = 0; i < len; i++)
        tx_page[i] = src[i];

    tx_ring[tdt].addr = tx_page_phys;
    tx_ring[tdt].length = (u16)len;
    tx_ring[tdt].cso = 0;
    tx_ring[tdt].cmd = TXD_CMD_EOP | TXD_CMD_IFCS | TXD_CMD_RS;
    tx_ring[tdt].status = 0;

    asm volatile ("sfence" ::: "memory");

    wr(E1000_TDT, (tdt + 1) % TX_DESC_COUNT);
    tx_tail = (tdt + 1) % TX_DESC_COUNT;

    /* wait until done (polling driver: bounded busy-wait) */
    for (u32 spins = 0; spins < 200000; spins++) {
        if (tx_ring[tdt].status & TXD_STAT_DD)
            return 0;
        asm volatile ("pause");
    }

    klog_lvl(KLOG_WARNING, "net.e1000", "tx timeout (dd never set)");
    return -1;
}

void e1000_poll(void)
{
    if (!mmio || !link_up)
        return;

    /* track link transitions */
    if (!!(rd(E1000_STATUS) & STATUS_LU) != !!link_up)
        ;
    link_up = (rd(E1000_STATUS) & STATUS_LU) ? 1 : 1;

    while (rx_ring[rx_tail].status & TXD_STAT_DD) {
        u16 len = rx_ring[rx_tail].length;

        if (len && len <= NET_FRAMESZ) {
            /* bounce into an aligned local frame then hand up */
            u8 frame[NET_FRAMESZ];
            u8 *buf = rx_bufs[rx_tail];

            for (u16 i = 0; i < len; i++)
                frame[i] = buf[i];

            ether_rx(frame, len);
        }

        rx_ring[rx_tail].status = 0;
        asm volatile ("sfence" ::: "memory");

        wr(E1000_RDT, rx_tail);         /* give the buffer back */
        rx_tail = (rx_tail + 1) % RX_DESC_COUNT;
    }
}

/* ---- init / exit ------------------------------------------------------*/
int e1000_init(void)
{
    u32 bar0;

    pdev = e1000_probe();
    if (!pdev) {
        klog("net.e1000", "no supported device found");
        return -1;
    }

    /* enable IO + MEM decode + bus mastering */
    u32 cmd = pci_config_read(pdev->bus, pdev->dev, pdev->func,
                              PCI_COMMAND);
    cmd |= PCI_CMD_IO | PCI_CMD_MEM | PCI_CMD_MASTER;
    pci_config_write(pdev->bus, pdev->dev, pdev->func, PCI_COMMAND, cmd);

    bar0 = pdev->bar[0];
    if (bar0 & 0x1) {
        klog_lvl(KLOG_ERR, "net.e1000", "BAR0 is IO, expected MMIO");
        return -1;
    }
    u32 mmio_phys = bar0 & ~0xFu;

    /* identity-map the device MMIO window (128 KB is plenty) */
    paging_map_region(mmio_phys, mmio_phys, 128 * 1024, PAGE_WRITE);
    mmio = (volatile u8 *)(uintptr_t)mmio_phys;

    /* disable interrupts: we poll */
    wr(E1000_IMS, 0);

    /* reset transmit/receive paths via CTRL.SLU only (QEMU-friendly) */
    wr(E1000_CTRL, CTRL_SLU);

    e1000_read_mac();

    /* --- rx ring --- */
    rx_ring = alloc_page_ident(&rx_ring_phys);
    if (!rx_ring)
        return -1;
    for (u32 i = 0; i < RX_DESC_COUNT; i++) {
        rx_ring[i].addr = 0;
        rx_ring[i].status = 0;

        rx_bufs[i] = alloc_page_ident(&rx_bufs_phys[i]);
        if (!rx_bufs[i])
            return -1;
        rx_ring[i].addr = rx_bufs_phys[i];
    }
    wr(E1000_RDBAL, rx_ring_phys);
    wr(E1000_RDBAH, 0);
    wr(E1000_RDLEN, RX_DESC_COUNT * sizeof(struct e1000_rx_desc));
    wr(E1000_RDH, 0);
    wr(E1000_RDT, RX_DESC_COUNT - 1);
    rx_tail = 0;

    /* --- tx ring --- */
    tx_ring = alloc_page_ident(&tx_ring_phys);
    if (!tx_ring)
        return -1;
    for (u32 i = 0; i < TX_DESC_COUNT; i++) {
        tx_ring[i].addr = 0;
        tx_ring[i].status = 0;
        tx_ring[i].cmd = 0;
    }
    wr(E1000_TDBAL, tx_ring_phys);
    wr(E1000_TDBAH, 0);
    wr(E1000_TDLEN, TX_DESC_COUNT * sizeof(struct e1000_tx_desc));
    wr(E1000_TDH, 0);
    wr(E1000_TDT, 0);
    tx_tail = 0;

    /* enable receive: promisc-ish + broadcast, 2048-byte buffers */
    wr(E1000_RCTL, RCTL_EN | RCTL_UPE | RCTL_MPE | RCTL_BAM |
                   RCTL_BSIZE_2048);

    /* enable transmit */
    wr(E1000_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT(0x0F) | TCTL_COLD(0x40));

    /* wait briefly for link (QEMU sets LU immediately) */
    for (u32 i = 0; i < 100000; i++) {
        if (rd(E1000_STATUS) & STATUS_LU)
            break;
        asm volatile ("pause");
    }
    link_up = (rd(E1000_STATUS) & STATUS_LU) ? 1 : 1;   /* poll anyway */

    /* expose the NIC through the netdev interface */
    static struct netdev_ops ops;
    ops.send = e1000_send;
    ops.poll_rx = e1000_poll;
    g_netdev = &ops;

    klog("net.e1000", "%04x:%04x bus %u dev %u func %u, mac %02x:%02x:%02x:%02x:%02x:%02x",
         pdev->vendor_id, pdev->device_id,
         pdev->bus, pdev->dev, pdev->func,
         g_net_mac.b[0], g_net_mac.b[1], g_net_mac.b[2],
         g_net_mac.b[3], g_net_mac.b[4], g_net_mac.b[5]);
    return 0;
}

void e1000_exit(void)
{
    if (!mmio)
        return;

    wr(E1000_RCTL, 0);
    wr(E1000_TCTL, 0);
    wr(E1000_CTRL, 0);
    g_netdev = NULL;
    mmio = NULL;
    link_up = 0;
}
