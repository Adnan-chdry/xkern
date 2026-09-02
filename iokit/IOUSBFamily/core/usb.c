#include "usb.h"
#include "../usb1/ohci.h"
#include "../usb1/uhci.h"
#include "../usb2/ehci.h"
#include "../usb3/xhci.h"
#include "../hub/usbhub.h"
#include "../hid/usbhid.h"
#include "../dev/usbdev.h"
#include "tsc.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include <stddef.h>
#include "klibc.h"
#include "pmm.h"
#include "paging.h"

static u32 usb_time_sigatt = USB_TIME_SIGATT;

/*
 * DMA pool.  Backed by pmm_alloc() frames, which the physical allocator
 * only ever hands out from below 3 GiB (see pmm.c: PMM_MAX_ADDR).  Each
 * frame is identity-mapped, so the CPU virtual address equals the bus
 * address the device sees - but callers must still go through
 * usb_dma_to_bus() so a real IOMMU can be inserted later.  Using pmm
 * frames (instead of a static .bss array) guarantees the memory is
 * DMA-able by 32-bit controllers and is never truncated.
 */
#define USB_DMA_PAGE_SIZE   4096
#define USB_DMA_MAX_PAGES   2048    /* up to 8 MiB of DMA pool */

struct usb_dma_page {
    u64 phys;           /* bus/physical address of this frame        */
    u32  used;          /* bump offset within the frame              */
};

static struct usb_dma_page g_dma_pages[USB_DMA_MAX_PAGES];
static u32 g_dma_npages;
static u32 g_dma_cur;           /* index of the page we carve from     */
static u64 g_dma_va_base;       /* inclusive low VA of the whole pool   */
static u64 g_dma_va_end;        /* exclusive high VA of the whole pool  */
static u32 g_dma_total;         /* bytes handed out (diagnostics)       */
static int g_dma_clflush_line;  /* cache line size in bytes (0=unknown) */
static int g_dma_has_clflush;

#ifndef ALIGN
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))
#endif

static void usb_dma_detect_cpu(void)
{
    if (g_dma_clflush_line)
        return;
    u32 eax = 1, ebx = 0, ecx = 0, edx = 0;
    asm volatile ("cpuid"
                  : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    g_dma_has_clflush = (edx & (1u << 19)) ? 1 : 0;
    /* CPUID.01 EBX[15:8] = CLFLUSH line size, in 8-byte units. */
    u32 linesz = ((ebx >> 8) & 0xff) * 8;
    g_dma_clflush_line = linesz ? linesz : 64;
}

static int usb_dma_grow_page(void)
{
    if (g_dma_npages >= USB_DMA_MAX_PAGES)
        return -1;
    u64 phys = pmm_alloc();
    if (!phys)
        return -1;
    /* Identity-map the frame so we can use it directly as a VA.  The
     * kernel's physmap usually already covers it, but mapping explicitly
     * is harmless and works regardless of where the kernel was loaded. */
    paging_map_page(phys, phys, PAGE_PRESENT | PAGE_WRITE);
    g_dma_pages[g_dma_npages].phys = phys;
    g_dma_pages[g_dma_npages].used = 0;
    if (!g_dma_va_base || phys < g_dma_va_base)
        g_dma_va_base = phys;
    if (phys + USB_DMA_PAGE_SIZE > g_dma_va_end)
        g_dma_va_end = phys + USB_DMA_PAGE_SIZE;
    g_dma_npages++;
    g_dma_cur = g_dma_npages - 1;
    return 0;
}

void usb_dma_pool_init(void)
{
    usb_dma_detect_cpu();
    /* Pre-fault one page so the very first allocation cannot fail. */
    if (g_dma_npages == 0)
        usb_dma_grow_page();
}

void usb_dma_reset(void)
{
    for (u32 i = 0; i < g_dma_npages; i++)
        g_dma_pages[i].used = 0;
    g_dma_cur = 0;
    g_dma_total = 0;
}

/*
 * Carve 'size' bytes aligned to 'align' out of the pool.  Returns a CPU
 * virtual address that is also the bus address (identity map).
 */
static u64 usb_dma_carve(u32 size, u32 align)
{
    if (align < 16)
        align = 16;
    if (g_dma_npages == 0 && usb_dma_grow_page() != 0)
        return 0;
    for (;;) {
        struct usb_dma_page *p = &g_dma_pages[g_dma_cur];
        u32 start = ALIGN(p->used, align);
        if (start + size <= USB_DMA_PAGE_SIZE) {
            p->used = start + size;
            g_dma_total += size;
            return p->phys + start;
        }
        if (g_dma_cur + 1 < g_dma_npages) {
            g_dma_cur++;
            continue;
        }
        if (usb_dma_grow_page() != 0)
            return 0;
    }
}

/*
 * Minimum alignment for implicit allocations.  Must satisfy the strictest
 * controller requirement among our users: OHCI's HCCA needs 256-byte
 * alignment and xHCI's command/event/transfer rings need 64-byte
 * alignment.  The original static pool used 256; keep that guarantee.
 */
#define USB_DMA_MIN_ALIGN 256

void *usb_dma_alloc(size_t size)
{
    u64 va = usb_dma_carve((u32)ALIGN(size, USB_DMA_MIN_ALIGN),
                           USB_DMA_MIN_ALIGN);
    if (!va)
        return NULL;
    void *p = (void *)va;
    klibc.memset(p, 0, size);
    return p;
}

void *usb_dma_alloc_aligned(size_t size, u32 align)
{
    if (align < USB_DMA_MIN_ALIGN)
        align = USB_DMA_MIN_ALIGN;
    u64 va = usb_dma_carve((u32)ALIGN(size, align), align);
    if (!va)
        return NULL;
    void *p = (void *)va;
    klibc.memset(p, 0, size);
    return p;
}

dma_addr_t usb_dma_to_bus(const void *va)
{
    return (dma_addr_t)(u64)va;
}

void *usb_dma_to_va(dma_addr_t dma)
{
    return (void *)(u64)dma;
}

/* ------------------------------------------------------------------ *
 * Cache maintenance.  All ops round the virtual range OUT to whole
 * cache lines so a partially-written (by the device) line is never left
 * sitting stale in the CPU cache.  sfence orders the flushes against the
 * device doorbell MMIO write that follows.
 * ------------------------------------------------------------------ */
void usb_dma_cache_clean(const void *va, u32 len)
{
    usb_dma_detect_cpu();
    if (!g_dma_has_clflush || !len)
        return;
    u32 line = g_dma_clflush_line;
    u64 a = (u64)va & ~(u64)(line - 1);
    u64 end = ((u64)va + len + line - 1) & ~(u64)(line - 1);
    for (; a < end; a += line)
        asm volatile ("clflush %0" : : "m"(*(volatile char *)a));
    asm volatile ("sfence" : : : "memory");
}

void usb_dma_cache_inv(void *va, u32 len)
{
    usb_dma_detect_cpu();
    if (!g_dma_has_clflush || !len)
        return;
    u32 line = g_dma_clflush_line;
    u64 a = (u64)va & ~(u64)(line - 1);
    u64 end = ((u64)va + len + line - 1) & ~(u64)(line - 1);
    for (; a < end; a += line)
        asm volatile ("clflush %0" : : "m"(*(volatile char *)a));
    asm volatile ("sfence" : : : "memory");
}

void usb_dma_cache_sync(void *va, u32 len, int dir)
{
    if (dir == USB_DMA_FROM_DEVICE)
        return;                 /* device owns it; inv deferred to unmap */
    usb_dma_cache_clean(va, len);
}

void usb_dma_wmb(void)
{
    asm volatile ("sfence" : : : "memory");
}

/* ------------------------------------------------------------------ *
 * Mapping API.  Default path is identity (pool memory is DMA-able).
 * With USB_DMA_BOUNCE defined, buffers that are not part of the DMA
 * pool are copied through a dedicated low-memory bounce page - this is
 * the IOMMU-style contract that decouples the device's view of memory
 * from the kernel's virtual address space.
 * ------------------------------------------------------------------ */
#ifdef USB_DMA_BOUNCE
static int usb_dma_is_pool_addr(const void *va, u32 len)
{
    u64 v = (u64)va;
    return (v >= g_dma_va_base && v + len <= g_dma_va_end);
}
#endif

#ifdef USB_DMA_BOUNCE
static void usb_dma_bounce_copy(struct usb_dma_map *m, int to_bounce)
{
    if (to_bounce)
        klibc.memcpy(m->bounce_va, m->va, m->len);
    else
        klibc.memcpy(m->va, m->bounce_va, m->len);
}
#endif

dma_addr_t usb_dma_map_single(void *va, u32 len, int dir,
                              struct usb_dma_map *m)
{
    m->va = va;
    m->len = len;
    m->dir = dir;
    m->bounced = 0;

#ifdef USB_DMA_BOUNCE
    if (!usb_dma_is_pool_addr(va, len)) {
        m->bounce_va = usb_dma_alloc_aligned(len, g_dma_clflush_line);
        if (!m->bounce_va)
            return 0;
        m->bounce_dma = usb_dma_to_bus(m->bounce_va);
        m->bounced = 1;
        if (dir != USB_DMA_FROM_DEVICE) {
            usb_dma_bounce_copy(m, 1);
            usb_dma_cache_clean(m->bounce_va, len);
        }
        return m->bounce_dma;
    }
#endif

    usb_dma_cache_sync(va, len, dir);
    m->dma = usb_dma_to_bus(va);
    return m->dma;
}

void usb_dma_sync_for_cpu(struct usb_dma_map *m)
{
    if (m->bounced) {
#ifdef USB_DMA_BOUNCE
        if (m->dir != USB_DMA_TO_DEVICE) {
            usb_dma_cache_inv(m->bounce_va, m->len);
            usb_dma_bounce_copy(m, 0);
        }
#endif
        return;
    }
    if (m->dir == USB_DMA_FROM_DEVICE || m->dir == USB_DMA_BIDIRECTIONAL)
        usb_dma_cache_inv(m->va, m->len);
}

void usb_dma_sync_for_device(struct usb_dma_map *m)
{
    if (m->bounced) {
#ifdef USB_DMA_BOUNCE
        if (m->dir != USB_DMA_FROM_DEVICE) {
            usb_dma_bounce_copy(m, 1);
            usb_dma_cache_clean(m->bounce_va, m->len);
        }
#endif
        return;
    }
    if (m->dir == USB_DMA_TO_DEVICE || m->dir == USB_DMA_BIDIRECTIONAL)
        usb_dma_cache_clean(m->va, m->len);
}

void usb_dma_unmap_single(struct usb_dma_map *m)
{
    usb_dma_sync_for_cpu(m);
}

void *usb_alloc(size_t size)
{
    return usb_dma_alloc(size);
}

void usb_free(void *ptr)
{
    (void)ptr;
}

/****************************************************************
 * Time helpers (TSC based, work with interrupts disabled)
 ****************************************************************/
u32 usb_now_ms(void)
{
    return (u32)tsc_ms();
}

void usb_msleep(int ms)
{
    u32 end = usb_now_ms() + ms;
    while ((s32)(usb_now_ms() - end) < 0)
        asm volatile ("pause");
}

/****************************************************************
 * Pipe freelist management
 ****************************************************************/
void usb_add_freelist(struct usb_pipe *pipe)
{
    if (!pipe)
        return;
    struct usb_s *cntl = pipe->cntl;
    pipe->freenext = cntl->freelist;
    cntl->freelist = pipe;
}

struct usb_pipe *usb_get_freelist(struct usb_s *cntl, u8 eptype)
{
    struct usb_pipe **pfree = &cntl->freelist;
    for (;;) {
        struct usb_pipe *pipe = *pfree;
        if (!pipe)
            return NULL;
        if (pipe->eptype == eptype) {
            *pfree = pipe->freenext;
            return pipe;
        }
        pfree = &pipe->freenext;
    }
}

void usb_desc2pipe(struct usb_pipe *pipe, struct usbdevice_s *usbdev,
                   struct usb_endpoint_descriptor *epdesc)
{
    pipe->cntl = usbdev->hub->cntl;
    pipe->type = usbdev->hub->cntl->type;
    pipe->ep = epdesc->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
    pipe->devaddr = usbdev->devaddr;
    pipe->speed = usbdev->speed;
    pipe->maxpacket = epdesc->wMaxPacketSize;
    pipe->eptype = epdesc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
}

/****************************************************************
 * Controller function wrappers
 ****************************************************************/
static struct usb_pipe *usb_realloc_pipe(struct usbdevice_s *usbdev,
                                         struct usb_pipe *pipe,
                                         struct usb_endpoint_descriptor *epdesc)
{
    switch (usbdev->hub->cntl->type) {
    case USB_TYPE_OHCI:
        return ohci_realloc_pipe(usbdev, pipe, epdesc);
    case USB_TYPE_EHCI:
        return ehci_realloc_pipe(usbdev, pipe, epdesc);
    case USB_TYPE_XHCI:
        return xhci_realloc_pipe(usbdev, pipe, epdesc);
    case USB_TYPE_UHCI:
        return uhci_realloc_pipe(usbdev, pipe, epdesc);
    default:
        return NULL;
    }
}

static int usb_send_pipe(struct usb_pipe *pipe_fl, int dir, const void *cmd,
                         void *data, int datasize)
{
    switch (pipe_fl->type) {
    case USB_TYPE_OHCI:
        return ohci_send_pipe(pipe_fl, dir, cmd, data, datasize);
    case USB_TYPE_EHCI:
        return ehci_send_pipe(pipe_fl, dir, cmd, data, datasize);
    case USB_TYPE_XHCI:
        return xhci_send_pipe(pipe_fl, dir, cmd, data, datasize);
    case USB_TYPE_UHCI:
        return uhci_send_pipe(pipe_fl, dir, cmd, data, datasize);
    default:
        return -1;
    }
}

int usb_poll_intr(struct usb_pipe *pipe_fl, void *data)
{
    switch (pipe_fl->type) {
    case USB_TYPE_OHCI:
        return ohci_poll_intr(pipe_fl, data);
    case USB_TYPE_EHCI:
        return ehci_poll_intr(pipe_fl, data);
    case USB_TYPE_XHCI:
        return xhci_poll_intr(pipe_fl, data);
    case USB_TYPE_UHCI:
        return uhci_poll_intr(pipe_fl, data);
    default:
        return -1;
    }
}

/* Bulk (or interrupt OUT) transfer without an embedded setup packet */
int usb_bulk_transfer(struct usb_pipe *pipe, int dir, void *data,
                      int datalen)
{
    return usb_send_pipe(pipe, dir, NULL, data, datalen);
}

struct usb_pipe *usb_alloc_pipe(struct usbdevice_s *usbdev,
                                struct usb_endpoint_descriptor *epdesc)
{
    return usb_realloc_pipe(usbdev, NULL, epdesc);
}

void usb_free_pipe(struct usbdevice_s *usbdev, struct usb_pipe *pipe)
{
    if (!pipe)
        return;
    usb_realloc_pipe(usbdev, pipe, NULL);
}

int usb_send_default_control(struct usb_pipe *pipe,
                             const struct usb_ctrlrequest *req, void *data)
{
    return usb_send_pipe(pipe, req->bRequestType & USB_DIR_IN, req,
                         data, req->wLength);
}

/****************************************************************
 * Descriptor helpers
 ****************************************************************/
int usb_get_period(struct usbdevice_s *usbdev,
                   struct usb_endpoint_descriptor *epdesc)
{
    int period = epdesc->bInterval;
    if (usbdev->speed != USB_HIGHSPEED)
        return (period <= 0) ? 0 : __fls(period);
    return (period <= 4) ? 0 : period - 4;
}

int usb_xfer_time(struct usb_pipe *pipe, int datalen)
{
    (void)datalen;
    if (!pipe->devaddr)
        return USB_TIME_STATUS + 100;
    return USB_TIME_COMMAND + 100;
}

struct usb_endpoint_descriptor *usb_find_desc(struct usbdevice_s *usbdev,
                                              int type, int dir)
{
    struct usb_endpoint_descriptor *epdesc = (void *)&usbdev->iface[1];
    for (;;) {
        if ((void *)epdesc >= (void *)usbdev->iface + usbdev->imax
            || epdesc->bDescriptorType == USB_DT_INTERFACE)
            return NULL;
        if (epdesc->bDescriptorType == USB_DT_ENDPOINT
            && (epdesc->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == dir
            && (epdesc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == type)
            return epdesc;
        epdesc = (void *)epdesc + epdesc->bLength;
    }
}

static int get_device_info8(struct usb_pipe *pipe,
                            struct usb_device_descriptor *dinfo)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_GET_DESCRIPTOR;
    req.wValue = USB_DT_DEVICE << 8;
    req.wIndex = 0;
    req.wLength = 8;
    int ret = usb_send_default_control(pipe, &req, dinfo);
    if (ret)
        klog("USB", "get_device_info8 failed ret=%d", ret);
    return ret;
}

static int get_device_info18(struct usb_pipe *pipe,
                             struct usb_device_descriptor *dinfo)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_GET_DESCRIPTOR;
    req.wValue = USB_DT_DEVICE << 8;
    req.wIndex = 0;
    req.wLength = sizeof(*dinfo);   /* full 18 bytes: VID/PID/iProduct */
    return usb_send_default_control(pipe, &req, dinfo);
}

static struct usb_config_descriptor *get_device_config(struct usb_pipe *pipe)
{
    struct usb_config_descriptor cfg;

    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_GET_DESCRIPTOR;
    req.wValue = USB_DT_CONFIG << 8;
    req.wIndex = 0;
    req.wLength = sizeof(cfg);
    int ret = usb_send_default_control(pipe, &req, &cfg);
    if (ret)
        return NULL;

    struct usb_config_descriptor *config = usb_alloc(cfg.wTotalLength);
    if (!config) {
        klog("USB", "no memory for config descriptor");
        return NULL;
    }
    klibc.memset(config, 0, cfg.wTotalLength);
    req.wLength = cfg.wTotalLength;
    ret = usb_send_default_control(pipe, &req, config);
    if (ret || config->wTotalLength != cfg.wTotalLength) {
        usb_free(config);
        return NULL;
    }
    return config;
}

static int set_configuration(struct usb_pipe *pipe, u16 val)
{
    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_SET_CONFIGURATION;
    req.wValue = val;
    req.wIndex = 0;
    req.wLength = 0;
    return usb_send_default_control(pipe, &req, NULL);
}

/****************************************************************
 * Address assignment
 ****************************************************************/
static const int speed_to_ctlsize[] = {
    [USB_FULLSPEED]  = 8,
    [USB_LOWSPEED]   = 8,
    [USB_HIGHSPEED]  = 64,
    [USB_SUPERSPEED] = 512,
};

static int usb_set_address(struct usbdevice_s *usbdev)
{
    struct usb_s *cntl = usbdev->hub->cntl;
    if (cntl->maxaddr >= USB_MAXADDR)
        return -1;

    usb_msleep(USB_TIME_RSTRCY);

    struct usb_endpoint_descriptor epdesc = {
        .wMaxPacketSize = speed_to_ctlsize[usbdev->speed],
        .bmAttributes = USB_ENDPOINT_XFER_CONTROL,
    };
    usbdev->defpipe = usb_alloc_pipe(usbdev, &epdesc);
    if (!usbdev->defpipe)
        return -1;

    struct usb_ctrlrequest req;
    req.bRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    req.bRequest = USB_REQ_SET_ADDRESS;
    req.wValue = cntl->maxaddr + 1;
    req.wIndex = 0;
    req.wLength = 0;
    int ret = usb_send_default_control(usbdev->defpipe, &req, NULL);
    if (ret) {
        usb_free_pipe(usbdev, usbdev->defpipe);
        return -1;
    }

    usb_msleep(USB_TIME_SETADDR_RECOVERY);

    cntl->maxaddr++;
    usbdev->devaddr = cntl->maxaddr;
    usbdev->defpipe = usb_realloc_pipe(usbdev, usbdev->defpipe, &epdesc);
    if (!usbdev->defpipe)
        return -1;
    return 0;
}

/****************************************************************
 * Device configuration and driver matching
 ****************************************************************/
#define USB_MAX_IFACES 8

static int configure_usb_device(struct usbdevice_s *usbdev)
{
    struct usb_device_descriptor dinfo;
    int ret = get_device_info8(usbdev->defpipe, &dinfo);
    if (ret)
        return 0;
    u16 maxpacket = dinfo.bMaxPacketSize0;
    /* USB 3.0 encodes bMaxPacketSize0 as log2 of the actual size */
    if (usbdev->speed == USB_SUPERSPEED && maxpacket < 512)
        maxpacket = 1 << maxpacket;
    klog("USB", "device rev=%04x cls=%02x sub=%02x proto=%02x size=%d",
         dinfo.bcdUSB, dinfo.bDeviceClass, dinfo.bDeviceSubClass,
         dinfo.bDeviceProtocol, maxpacket);
    if (maxpacket < 8)
        return 0;

    struct usb_endpoint_descriptor epdesc = {
        .wMaxPacketSize = maxpacket,
        .bmAttributes = USB_ENDPOINT_XFER_CONTROL,
    };
    usbdev->defpipe = usb_realloc_pipe(usbdev, usbdev->defpipe, &epdesc);
    if (!usbdev->defpipe)
        return -1;

    /* full descriptor - VID/PID/iProduct for the device registry */
    struct usb_device_descriptor dd;
    int have_dd = (get_device_info18(usbdev->defpipe, &dd) == 0
                   && dd.bLength >= sizeof(dd));

    struct usb_config_descriptor *config = get_device_config(usbdev->defpipe);
    if (!config)
        return 0;

    /* collect every interface: composite devices enumerate fully */
    void *config_end = (void *)config + config->wTotalLength;
    struct usb_interface_descriptor *ifaces[USB_MAX_IFACES];
    int nifaces = 0;
    for (u8 *p = (u8 *)&config[1]; p < (u8 *)config_end; ) {
        struct usb_interface_descriptor *ifd = (void *)p;
        u8 len = ifd->bLength;
        if (len < 2 || (void *)p + len > config_end)
            break;
        if (ifd->bDescriptorType == USB_DT_INTERFACE) {
            if (len < sizeof(*ifd) || nifaces == USB_MAX_IFACES)
                break;
            ifaces[nifaces++] = ifd;
        }
        p += len;
    }

    ret = set_configuration(usbdev->defpipe, config->bConfigurationValue);
    if (ret)
        goto fail;

    /* attach one driver per interface */
    int count = 0;
    int first_type = 0;
    for (int i = 0; i < nifaces; i++) {
        struct usbdevice_s sub = *usbdev;
        sub.iface = ifaces[i];
        void *next = config_end;
        for (int j = i + 1; j < nifaces; j++) {
            next = (void *)ifaces[j];
            break;
        }
        sub.imax = (void *)next - (void *)ifaces[i];

        int type = usbdev_attach(&sub);
        if (type < 0)
            continue;
        count++;
        if (!first_type)
            first_type = type;
        usbdev_register(&sub, type,
                        have_dd ? dd.idVendor : 0,
                        have_dd ? dd.idProduct : 0,
                        have_dd ? dd.iProduct : 0);
    }

    if (!count)
        goto fail;

    /* keep canonical view on the primary interface for callers */
    if (first_type == USBDEV_HUB) {
        for (int i = 0; i < nifaces; i++) {
            if (usbdev_classify(ifaces[i]) == USBDEV_HUB) {
                usbdev->iface = ifaces[i];
                usbdev->imax = (void *)config_end - (void *)ifaces[i];
                break;
            }
        }
    } else {
        usbdev->iface = ifaces[0];
        usbdev->imax = (void *)config_end - (void *)ifaces[0];
    }

    usb_free(config);
    return count;

fail:
    usb_free(config);
    return 0;
}

/****************************************************************
 * Port setup (SeaBIOS usb_hub_port_setup, poll based)
 ****************************************************************/
static void usb_hub_port_setup(struct usbdevice_s *usbdev)
{
    struct usbhub_s *hub = usbdev->hub;
    u32 port = usbdev->port;

    for (;;) {
        int ret = hub->op->detect(hub, port);
        if (ret > 0)
            break;
        if (ret < 0 || usb_now_ms() >= hub->detectend)
            goto done;
        usb_msleep(5);
    }

    int ret = hub->op->reset(hub, port);
    if (ret < 0)
        goto resetfail;
    usbdev->speed = ret;
    klog("USB", "port %d: speed=%d", port, ret);

    ret = usb_set_address(usbdev);
    if (ret) {
        hub->op->disconnect(hub, port);
        goto resetfail;
    }
    klog("USB", "port %d: address=%d", port, usbdev->devaddr);

    int count = configure_usb_device(usbdev);
    usb_free_pipe(usbdev, usbdev->defpipe);
    if (!count)
        hub->op->disconnect(hub, port);
    hub->devcount += count;
done:
    hub->threads--;
    usb_free(usbdev);
    return;

resetfail:
    hub->op->disconnect(hub, port);
    goto done;
}

/****************************************************************
 * Hub enumeration
 ****************************************************************/
void usb_enumerate(struct usbhub_s *hub)
{
    u32 portcount = hub->portcount;
    hub->threads = portcount;
    hub->detectend = usb_now_ms() + usb_time_sigatt;

    for (u32 i = 0; i < portcount; i++) {
        struct usbdevice_s *usbdev = usb_alloc(sizeof(*usbdev));
        if (!usbdev)
            continue;
        klibc.memset(usbdev, 0, sizeof(*usbdev));
        usbdev->hub = hub;
        usbdev->port = i;
        usb_hub_port_setup(usbdev);
    }
}

void usb_setup(void)
{
    klog("USB", "core setup");
    int xr = xhci_setup();
    if (xr != 0 || usbdev_count() == 0) {
        /* xHCI absent or found nothing - the ports may live on the
         * companion EHCI/OHCI/UHCI controllers (firmware-owned on many
         * boards) */
        ehci_setup();
        ohci_setup();
        uhci_setup();
    }
    usbdev_dump();
}
