#include "usbmsc.h"
#include "IOStorageFamily/devfs/devfs.h"
#include "stdio.h"
#include "string.h"
#include "klog.h"
#include "klibc.h"

/* BOT protocol constants */
#define CBW_SIGNATURE 0x43425355u   /* 'USBC' */
#define CSW_SIGNATURE 0x53425355u   /* 'USBS' */
#define CSW_STATUS_OK      0
#define CSW_STATUS_FAILED  1
#define CSW_STATUS_PHASE   2

#define CBW_SIZE 31
#define CSW_SIZE 13

/* SCSI opcodes */
#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_REQUEST_SENSE   0x03
#define SCSI_INQUIRY         0x12
#define SCSI_READ_CAPACITY   0x25
#define SCSI_READ_10         0x28
#define SCSI_WRITE_10        0x2A

/*
 * xHCI: data buffers must not cross a 64KB boundary without chained
 * TRBs.  QEMU tolerates it, real hardware does not - chunk transfers.
 */
#define MSC_MAX_CHUNK (32 * 1024)

struct usbmsc_dev {
    struct usb_pipe *bulk_in;
    struct usb_pipe *bulk_out;
    u32 block_len;
    u32 block_count;
    char model[48];
};

struct cbw_pkt {
    u32 sig;
    u32 tag;
    u32 datalen;
    u8  flags;
    u8  lun;
    u8  cblen;
    u8  cb[16];
} __attribute__((packed));

struct csw_pkt {
    u32 sig;
    u32 tag;
    u32 residue;
    u8  status;
} __attribute__((packed));

static u32 g_tag;
static int g_disk_count;

static void scsi_be16(u8 *p, u16 v)
{
    p[0] = v >> 8;
    p[1] = v;
}

static void scsi_be32(u8 *p, u32 v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static u32 be_to_cpu32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
           ((u32)p[2] << 8) | p[3];
}

/* Mass Storage Reset (class specific) + clear halt on both endpoints */
static int msc_reset(struct usbdevice_s *usbdev)
{
    struct usb_ctrlrequest req;

    req.bRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    req.bRequest = 0xFF;        /* BULK_ONLY_MASS_STORAGE_RESET */
    req.wValue = 0;
    req.wIndex = usbdev->iface->bInterfaceNumber;
    req.wLength = 0;
    if (usb_send_default_control(usbdev->defpipe, &req, NULL))
        return -1;

    struct usb_endpoint_descriptor *ep;
    ep = usb_find_desc(usbdev, USB_ENDPOINT_XFER_BULK, USB_DIR_IN);
    if (ep) {
        req.bRequestType = USB_DIR_OUT | USB_TYPE_STANDARD
                           | USB_RECIP_ENDPOINT;
        req.bRequest = USB_REQ_CLEAR_FEATURE;
        req.wValue = 0;         /* ENDPOINT_HALT */
        req.wIndex = ep->bEndpointAddress;
        req.wLength = 0;
        usb_send_default_control(usbdev->defpipe, &req, NULL);
    }
    ep = usb_find_desc(usbdev, USB_ENDPOINT_XFER_BULK, USB_DIR_OUT);
    if (ep) {
        req.bRequestType = USB_DIR_OUT | USB_TYPE_STANDARD
                           | USB_RECIP_ENDPOINT;
        req.bRequest = USB_REQ_CLEAR_FEATURE;
        req.wValue = 0;
        req.wIndex = ep->bEndpointAddress;
        req.wLength = 0;
        usb_send_default_control(usbdev->defpipe, &req, NULL);
    }
    return 0;
}

/* One BOT transaction: CBW -> optional data -> CSW */
static int msc_command(struct usbmsc_dev *d, const u8 *cb, u8 cblen,
                       int dir, void *data, u32 datalen)
{
    struct cbw_pkt cbw;
    struct csw_pkt csw;

    klibc.memset(&cbw, 0, sizeof(cbw));
    cbw.sig = CBW_SIGNATURE;
    cbw.tag = ++g_tag;
    cbw.datalen = datalen;
    cbw.flags = (dir == USB_DIR_IN && datalen) ? 0x80 : 0;
    cbw.lun = 0;
    cbw.cblen = cblen;
    klibc.memcpy(cbw.cb, cb, cblen);

    if (usb_bulk_transfer(d->bulk_out, USB_DIR_OUT,
                      &cbw, sizeof(cbw))) {
        klog("USBMSC", "cbw send failed");
        return -1;
    }

    if (datalen) {
        void *p = data;
        u32 done = 0;
        while (done < datalen) {
            /* keep chunks inside one 64KB window for real hardware */
            u32 addr = (u32)(unsigned long)p + done;
            u32 room = 0x10000 - (addr & 0xffff);
            u32 len = datalen - done;
            if (len > MSC_MAX_CHUNK)
                len = MSC_MAX_CHUNK;
            if (len > room)
                len = room;
            struct usb_pipe *dp =
                (dir == USB_DIR_IN) ? d->bulk_in : d->bulk_out;
            if (usb_bulk_transfer(dp, dir, (char *)p + done, len)) {
                klog("USBMSC", "data xfer failed (%u/%u)", done, datalen);
                return -1;
            }
            done += len;
        }
    }

    if (usb_bulk_transfer(d->bulk_in, USB_DIR_IN, &csw, CSW_SIZE)) {
        klog("USBMSC", "csw recv failed");
        return -1;
    }

    if (csw.sig != CSW_SIGNATURE || csw.tag != cbw.tag) {
        klog("USBMSC", "bad csw sig=%08x tag=%u", csw.sig, csw.tag);
        return -1;
    }
    if (csw.status != CSW_STATUS_OK) {
        klog("USBMSC", "command %02x: csw status %d", cb[0], csw.status);
        return -1;
    }
    return 0;
}

/* Check-condition handling: read sense data after a failed command */
static void msc_request_sense(struct usbmsc_dev *d)
{
    u8 cb[16] = { SCSI_REQUEST_SENSE };
    u8 sense[18];

    klibc.memset(sense, 0, sizeof(sense));
    scsi_be16(cb + 7, sizeof(sense));   /* allocation length */
    if (msc_command(d, cb, 6, USB_DIR_IN, sense, sizeof(sense)))
        return;

    klog("USBMSC", "sense sk=%02x asc=%02x ascq=%02x",
         sense[2], sense[12], sense[13]);
}

static int scsi_test_unit_ready(struct usbmsc_dev *d)
{
    u8 cb[16] = { SCSI_TEST_UNIT_READY };
    return msc_command(d, cb, 6, USB_DIR_OUT, NULL, 0);
}

static int scsi_inquiry(struct usbmsc_dev *d, u8 inquiry[36])
{
    u8 cb[16] = { SCSI_INQUIRY };

    klibc.memset(inquiry, 0, 36);
    cb[4] = 36;                 /* allocation length */
    return msc_command(d, cb, 6, USB_DIR_IN, inquiry, 36);
}

static int scsi_read_capacity(struct usbmsc_dev *d,
                              u32 *nblocks, u32 *blocklen)
{
    u8 cb[16] = { SCSI_READ_CAPACITY };
    u8 data[8];

    klibc.memset(data, 0, sizeof(data));
    if (msc_command(d, cb, 10, USB_DIR_IN, data, sizeof(data)))
        return -1;

    *nblocks = be_to_cpu32(data);       /* last LBA */
    *blocklen = be_to_cpu32(data + 4);
    return 0;
}

/* Split a big R/W into 64KB-safe chunks; count is in blocks */
static int scsi_rw10(struct usbmsc_dev *d, int write,
                     u32 lba, u32 nblocks, void *buf)
{
    u8 cb[16];
    u32 bl = d->block_len;
    char *p = buf;
    u32 remaining = nblocks;

    while (remaining) {
        u32 addr = (u32)(unsigned long)p;
        u32 max_by_addr = (0x10000 - (addr & 0xffff)) / bl;
        u32 chunk = remaining;
        if (chunk > MSC_MAX_CHUNK / bl)
            chunk = MSC_MAX_CHUNK / bl;
        if (!chunk)
            chunk = 1;          /* block larger than 64K - pathological */
        if (chunk > max_by_addr)
            chunk = max_by_addr;
        u32 bytes = chunk * bl;

        klibc.memset(cb, 0, sizeof(cb));
        cb[0] = write ? SCSI_WRITE_10 : SCSI_READ_10;
        scsi_be32(cb + 2, lba);
        scsi_be16(cb + 7, chunk);

        if (msc_command(d, cb, 10,
                        write ? USB_DIR_OUT : USB_DIR_IN, p, bytes)) {
            msc_request_sense(d);
            return -1;
        }
        lba += chunk;
        p += bytes;
        remaining -= chunk;
    }
    return 0;
}

static int usbdisk_read(struct devfs_device *dev, u32 lba, u8 count,
                        void *buf)
{
    struct usbmsc_dev *d = dev->priv;
    if (!d || lba + count > d->block_count)
        return -1;
    return scsi_rw10(d, 0, lba, count, buf);
}

static int usbdisk_write(struct devfs_device *dev, u32 lba, u8 count,
                         void *buf)
{
    struct usbmsc_dev *d = dev->priv;
    if (!d || lba + count > d->block_count)
        return -1;
    return scsi_rw10(d, 1, lba, count, buf);
}

int usb_msc_setup(struct usbdevice_s *usbdev)
{
    struct usb_endpoint_descriptor *in_ep =
        usb_find_desc(usbdev, USB_ENDPOINT_XFER_BULK, USB_DIR_IN);
    struct usb_endpoint_descriptor *out_ep =
        usb_find_desc(usbdev, USB_ENDPOINT_XFER_BULK, USB_DIR_OUT);
    if (!in_ep || !out_ep) {
        klog("USBMSC", "missing bulk endpoints");
        return -1;
    }

    struct usbmsc_dev *d = usb_alloc(sizeof(*d));
    if (!d)
        return -1;
    klibc.memset(d, 0, sizeof(*d));
    d->bulk_in = usb_alloc_pipe(usbdev, in_ep);
    d->bulk_out = usb_alloc_pipe(usbdev, out_ep);
    if (!d->bulk_in || !d->bulk_out)
        goto fail;

    /* reset protocol state before first command (real HW requirement) */
    msc_reset(usbdev);
    usb_msleep(50);

    u8 inquiry[36];
    if (scsi_inquiry(d, inquiry))
        goto fail;

    /* wait for medium to become ready (spinning up on real drives) */
    int ready = 0;
    for (int i = 0; i < 20 && !ready; i++) {
        if (!scsi_test_unit_ready(d)) {
            ready = 1;
            break;
        }
        msc_request_sense(d);
        usb_msleep(250);
    }
    if (!ready) {
        klog("USBMSC", "device not ready");
        goto fail;
    }

    u32 nblocks = 0, blocklen = 0;
    if (scsi_read_capacity(d, &nblocks, &blocklen))
        goto fail;
    if (!blocklen || blocklen > 4096)
        blocklen = 512;
    d->block_len = blocklen;
    d->block_count = nblocks + 1;

    char name[DEVFS_NAME_MAX];
    klibc.snprintf(name, sizeof(name), "ud%d", g_disk_count++);
    klibc.snprintf(d->model, sizeof(d->model), "%.8s %.16s",
                   &inquiry[8], &inquiry[16]);

    struct devfs_device ddev;
    klibc.memset(&ddev, 0, sizeof(ddev));
    klibc.strncpy(ddev.name, name, DEVFS_NAME_MAX - 1);
    ddev.type = DEVFS_BLOCK_DEV;
    ddev.block_size = blocklen;
    ddev.block_count = d->block_count;
    klibc.strncpy(ddev.model, d->model, sizeof(ddev.model) - 1);
    ddev.priv = d;
    ddev.read = usbdisk_read;
    ddev.write = usbdisk_write;
    devfs_register(&ddev);

    klog("USBMSC", "%s: %u MB (%u x %u byte sectors)",
         name, (u32)((u64)d->block_count * blocklen >> 20),
         d->block_count, blocklen);
    return 0;

fail:
    if (d->bulk_in)
        usb_free_pipe(usbdev, d->bulk_in);
    if (d->bulk_out)
        usb_free_pipe(usbdev, d->bulk_out);
    return -1;
}
