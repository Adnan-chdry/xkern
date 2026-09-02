#ifndef XHCI_H
#define XHCI_H

#include "types.h"
#include "../core/usb.h"

#define XHCI_RING_ITEMS          16
#define XHCI_RING_SIZE           (XHCI_RING_ITEMS * sizeof(struct xhci_trb))

#define XHCI_CMD_RS              (1<<0)
#define XHCI_CMD_HCRST           (1<<1)
#define XHCI_CMD_INTE            (1<<2)
#define XHCI_CMD_HSEE            (1<<3)
#define XHCI_CMD_LHCRST          (1<<7)
#define XHCI_CMD_CSS             (1<<8)
#define XHCI_CMD_CRS             (1<<9)
#define XHCI_CMD_EWE             (1<<10)
#define XHCI_CMD_EU3S            (1<<11)

#define XHCI_STS_HCH             (1<<0)
#define XHCI_STS_HSE             (1<<2)
#define XHCI_STS_EINT            (1<<3)
#define XHCI_STS_PCD             (1<<4)
#define XHCI_STS_SSS             (1<<8)
#define XHCI_STS_RSS             (1<<9)
#define XHCI_STS_SRE             (1<<10)
#define XHCI_STS_CNR             (1<<11)
#define XHCI_STS_HCE             (1<<12)

#define XHCI_PORTSC_CCS          (1<<0)
#define XHCI_PORTSC_PED          (1<<1)
#define XHCI_PORTSC_OCA          (1<<3)
#define XHCI_PORTSC_PR           (1<<4)
#define XHCI_PORTSC_PLS_SHIFT        5
#define XHCI_PORTSC_PLS_MASK     0xf
#define XHCI_PORTSC_PP           (1<<9)
#define XHCI_PORTSC_SPEED_SHIFT      10
#define XHCI_PORTSC_SPEED_MASK   0xf
#define XHCI_PORTSC_SPEED_FULL   (1<<10)
#define XHCI_PORTSC_SPEED_LOW    (2<<10)
#define XHCI_PORTSC_SPEED_HIGH   (3<<10)
#define XHCI_PORTSC_SPEED_SUPER  (4<<10)
#define XHCI_PORTSC_LWS          (1<<16)
#define XHCI_PORTSC_CSC          (1<<17)
#define XHCI_PORTSC_PEC          (1<<18)
#define XHCI_PORTSC_WRC          (1<<19)
#define XHCI_PORTSC_OCC          (1<<20)
#define XHCI_PORTSC_PRC          (1<<21)
#define XHCI_PORTSC_PLC          (1<<22)
#define XHCI_PORTSC_CEC          (1<<23)

#define TRB_C                    (1<<0)
#define TRB_TYPE_SHIFT           10
#define TRB_TYPE_MASK            0x3f
#define TRB_TYPE(t)              (((t) >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK)

#define TRB_EV_ED                (1<<2)

#define TRB_TR_ENT               (1<<1)
#define TRB_TR_ISP               (1<<2)
#define TRB_TR_NS                (1<<3)
#define TRB_TR_CH                (1<<4)
#define TRB_TR_IOC               (1<<5)
#define TRB_TR_IDT               (1<<6)
#define TRB_TR_BEI               (1<<9)

#define TRB_TR_DIR               (1<<16)

#define TRB_CR_SLOTID_SHIFT      24
#define TRB_CR_SLOTID_MASK       0xff
#define TRB_CR_EPID_SHIFT        16
#define TRB_CR_EPID_MASK         0x1f

#define TRB_CR_BSR               (1<<9)
#define TRB_CR_DC                (1<<9)

#define TRB_LK_TC                (1<<1)

#define XHCI_IR_EHB              (1<<3)
#define XHCI_IMAN_IP             (1<<0)   /* interrupt pending (RW1C) */
#define XHCI_IMAN_IE             (1<<1)   /* interrupt enable */
#define XHCI_HCC_PPC             (1<<3)

#define TRB_INTR_SHIFT           22
#define TRB_INTR_MASK            0x3ff

typedef enum TRBType {
    TRB_RESERVED = 0,
    TR_NORMAL,
    TR_SETUP,
    TR_DATA,
    TR_STATUS,
    TR_ISOCH,
    TR_LINK,
    TR_EVDATA,
    TR_NOOP,
    CR_ENABLE_SLOT,
    CR_DISABLE_SLOT,
    CR_ADDRESS_DEVICE,
    CR_CONFIGURE_ENDPOINT,
    CR_EVALUATE_CONTEXT,
    CR_RESET_ENDPOINT,
    CR_STOP_ENDPOINT,
    CR_SET_TR_DEQUEUE,
    CR_RESET_DEVICE,
    CR_FORCE_EVENT,
    CR_NEGOTIATE_BW,
    CR_SET_LATENCY_TOLERANCE,
    CR_GET_PORT_BANDWIDTH,
    CR_FORCE_HEADER,
    CR_NOOP,
    ER_TRANSFER = 32,
    ER_COMMAND_COMPLETE,
    ER_PORT_STATUS_CHANGE,
    ER_BANDWIDTH_REQUEST,
    ER_DOORBELL,
    ER_HOST_CONTROLLER,
    ER_DEVICE_NOTIFICATION,
    ER_MFINDEX_WRAP,
} TRBType;

typedef enum TRBCCode {
    CC_INVALID = 0,
    CC_SUCCESS,
    CC_DATA_BUFFER_ERROR,
    CC_BABBLE_DETECTED,
    CC_USB_TRANSACTION_ERROR,
    CC_TRB_ERROR,
    CC_STALL_ERROR,
    CC_RESOURCE_ERROR,
    CC_BANDWIDTH_ERROR,
    CC_NO_SLOTS_ERROR,
    CC_INVALID_STREAM_TYPE_ERROR,
    CC_SLOT_NOT_ENABLED_ERROR,
    CC_EP_NOT_ENABLED_ERROR,
    CC_SHORT_PACKET,
    CC_RING_UNDERRUN,
    CC_RING_OVERRUN,
    CC_VF_ER_FULL,
    CC_PARAMETER_ERROR,
    CC_BANDWIDTH_OVERRUN,
    CC_CONTEXT_STATE_ERROR,
    CC_NO_PING_RESPONSE_ERROR,
    CC_EVENT_RING_FULL_ERROR,
    CC_INCOMPATIBLE_DEVICE_ERROR,
    CC_MISSED_SERVICE_ERROR,
    CC_COMMAND_RING_STOPPED,
    CC_COMMAND_ABORTED,
    CC_STOPPED,
    CC_STOPPED_LENGTH_INVALID,
    CC_MAX_EXIT_LATENCY_TOO_LARGE_ERROR = 29,
    CC_ISOCH_BUFFER_OVERRUN = 31,
    CC_EVENT_LOST_ERROR,
    CC_UNDEFINED_ERROR,
    CC_INVALID_STREAM_ID_ERROR,
    CC_SECONDARY_BANDWIDTH_ERROR,
    CC_SPLIT_TRANSACTION_ERROR
} TRBCCode;

enum {
    PLS_U0              =  0,
    PLS_U1              =  1,
    PLS_U2              =  2,
    PLS_U3              =  3,
    PLS_DISABLED        =  4,
    PLS_RX_DETECT       =  5,
    PLS_INACTIVE        =  6,
    PLS_POLLING         =  7,
    PLS_RECOVERY        =  8,
    PLS_HOT_RESET       =  9,
    PLS_COMPILANCE_MODE = 10,
    PLS_TEST_MODE       = 11,
    PLS_RESUME          = 15,
};

#define xhci_get_field(data, field)             \
    (((data) >> field##_SHIFT) & field##_MASK)

/* ------------------------------------------------------------------ */
/* register interface                                                  */

struct xhci_caps {
    u8  caplength;
    u8  reserved_01;
    u16 hciversion;
    u32 hcsparams1;
    u32 hcsparams2;
    u32 hcsparams3;
    u32 hccparams;
    u32 dboff;
    u32 rtsoff;
};

struct xhci_op {
    u32 usbcmd;
    u32 usbsts;
    u32 pagesize;
    u32 reserved_01[2];
    u32 dnctl;
    u32 crcr_low;
    u32 crcr_high;
    u32 reserved_02[4];
    u32 dcbaap_low;
    u32 dcbaap_high;
    u32 config;
};

struct xhci_pr {
    u32 portsc;
    u32 portpmsc;
    u32 portli;
    u32 reserved_01;
};

struct xhci_db {
    u32 doorbell;
};

struct xhci_ir {
    u32 iman;
    u32 imod;
    u32 erstsz;
    u32 reserved_01;
    u32 erstba_low;
    u32 erstba_high;
    u32 erdp_low;
    u32 erdp_high;
};

/* ------------------------------------------------------------------ */
/* memory data structs                                                 */

struct xhci_slotctx {
    u32 ctx[4];
    u32 reserved_01[4];
} __attribute__((packed));

struct xhci_epctx {
    u32 ctx[2];
    u32 deq_low;
    u32 deq_high;
    u32 length;
    u32 reserved_01[3];
} __attribute__((packed));

struct xhci_devlist {
    u32 ptr_low;
    u32 ptr_high;
} __attribute__((packed));

struct xhci_inctx {
    u32 del;
    u32 add;
    u32 reserved_01[6];
} __attribute__((packed));

struct xhci_trb {
    u32 ptr_low;
    u32 ptr_high;
    u32 status;
    u32 control;
} __attribute__((packed));

struct xhci_er_seg {
    u32 ptr_low;
    u32 ptr_high;
    u32 size;
    u32 reserved_01;
} __attribute__((packed));

/* ------------------------------------------------------------------ */
/* driver state                                                        */

struct xhci_ring {
    struct xhci_trb ring[XHCI_RING_ITEMS];
    struct xhci_trb evt;
    u32 eidx;
    u32 nidx;
    u32 cs;
};

struct usb_xhci_s {
    struct usb_s usb;

    u32 ports;
    u32 slots;
    u8  context64;

    volatile struct xhci_caps *caps;
    volatile struct xhci_op   *op;
    volatile struct xhci_pr   *pr;
    volatile struct xhci_ir   *ir;
    volatile struct xhci_db   *db;

    struct xhci_devlist *devs;
    struct xhci_ring    *cmds;
    struct xhci_ring    *evts;
    struct xhci_er_seg  *eseg;
};

struct xhci_pipe {
    struct xhci_ring reqs;

    struct usb_pipe pipe;
    u32 slotid;
    u32 epid;
    void *buf;
    int  bufused;
};

int xhci_setup(void);
struct usb_pipe *xhci_realloc_pipe(struct usbdevice_s *usbdev,
                                   struct usb_pipe *upipe,
                                   struct usb_endpoint_descriptor *epdesc);
int xhci_send_pipe(struct usb_pipe *p, int dir, const void *cmd,
                   void *data, int datalen);
int xhci_poll_intr(struct usb_pipe *p, void *data);

#endif
