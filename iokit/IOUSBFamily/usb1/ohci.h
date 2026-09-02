#ifndef OHCI_H
#define OHCI_H

#include "types.h"
#include "../core/usb.h"

/****************************************************************
 * PCI identification
 ****************************************************************/
#define OHCI_PCI_CLASS    0x0C
#define OHCI_PCI_SUBCLASS 0x03
#define OHCI_PCI_PROGIF   0x10

/****************************************************************
 * OHCI register offsets
 ****************************************************************/
#define OHCI_HC_REVISION       0x00
#define OHCI_HC_CONTROL        0x04
#define OHCI_HC_CMDSTATUS      0x08
#define OHCI_HC_INTSTATUS      0x0C
#define OHCI_HC_INTENABLE      0x10
#define OHCI_HC_INTDISABLE     0x14
#define OHCI_HC_HCCA           0x18
#define OHCI_HC_PERIOD_ED      0x1C
#define OHCI_HC_CTRL_HEAD_ED   0x20
#define OHCI_HC_CTRL_CUR_ED    0x24
#define OHCI_HC_BULK_HEAD_ED   0x28
#define OHCI_HC_BULK_CUR_ED    0x2C
#define OHCI_HC_DONE_HEAD      0x30
#define OHCI_HC_FM_INTERVAL    0x34
#define OHCI_HC_FM_REMAINING   0x38
#define OHCI_HC_FM_NUMBER      0x3C
#define OHCI_HC_PERIODIC_START 0x40
#define OHCI_HC_LS_THRESHOLD   0x44
#define OHCI_HC_RH_DESC_A      0x48
#define OHCI_HC_RH_DESC_B      0x4C
#define OHCI_HC_RH_STATUS      0x50
#define OHCI_HC_RH_PORT_STATUS 0x54

#define FIT                     (1 << 31)

#define OHCI_CTRL_CBSR  (3 << 0)
#define OHCI_CTRL_PLE   (1 << 2)
#define OHCI_CTRL_CLE   (1 << 4)
#define OHCI_CTRL_BLE   (1 << 5)
#define OHCI_CTRL_HCFS  (3 << 6)
#define OHCI_USB_RESET  (0 << 6)
#define OHCI_USB_OPER   (2 << 6)
#define OHCI_CTRL_RWC   (1 << 9)
#define OHCI_CTRL_MIE   (1 << 31)

#define OHCI_HCR        (1 << 0)
#define OHCI_CLF        (1 << 1)
#define OHCI_BLF        (1 << 2)

#define OHCI_INT_MIE    (1 << 31)

#define RH_PS_CCS      0x00000001
#define RH_PS_PES      0x00000002
#define RH_PS_PSS      0x00000004
#define RH_PS_POCI     0x00000008
#define RH_PS_PRS      0x00000010
#define RH_PS_PPS      0x00000100
#define RH_PS_LSDA     0x00000200
#define RH_PS_CSC      0x00010000
#define RH_PS_PESC     0x00020000
#define RH_PS_PSSC     0x00040000
#define RH_PS_OCIC     0x00080000
#define RH_PS_PRSC     0x00100000

#define RH_HS_LPS      0x00000001
#define RH_HS_OCI      0x00000002
#define RH_HS_DRWE     0x00008000
#define RH_HS_LPSC     0x00010000
#define RH_HS_OCIC     0x00020000
#define RH_HS_CRWE     0x80000000

#define RH_B_DR        0x0000ffff
#define RH_B_PPCM      0xffff0000

#define RH_A_NDP       (0xff << 0)
#define RH_A_PSM       (1 << 8)
#define RH_A_NPS       (1 << 9)
#define RH_A_DT        (1 << 10)
#define RH_A_OCPM      (1 << 11)
#define RH_A_NOCP      (1 << 12)
#define RH_A_POTPGT    (0xff << 24)

/****************************************************************
 * OHCI structures
 ****************************************************************/
struct ohci_ed {
    u32 hwINFO;
    u32 hwTailP;
    u32 hwHeadP;
    u32 hwNextED;
} __attribute__((packed));

#define ED_ISO      (1 << 15)
#define ED_SKIP     (1 << 14)
#define ED_LOWSPEED (1 << 13)
#define ED_OUT      (0x01 << 11)
#define ED_IN       (0x02 << 11)
#define ED_C        (0x02)
#define ED_H        (0x01)

struct ohci_td {
    u32 hwINFO;
    u32 hwCBP;
    u32 hwNextTD;
    u32 hwBE;
} __attribute__((packed));

#define TD_CC       0xf0000000
#define TD_CC_GET(td) ((td >> 28) & 0x0f)
#define TD_DI       0x00e00000
#define TD_DONE     0x00020000
#define TD_ISO      0x00010000
#define TD_EC       0x0c000000
#define TD_T        0x03000000
#define TD_T_DATA0  0x02000000
#define TD_T_DATA1  0x03000000
#define TD_T_TOGGLE 0x00000000
#define TD_DP       0x00180000
#define TD_DP_SETUP 0x00000000
#define TD_DP_IN    0x00100000
#define TD_DP_OUT   0x00080000
#define TD_R        0x00040000

struct ohci_hcca {
    u32  int_table[32];
    u32  frame_no;
    u32  done_head;
    u8   reserved[120];
} __attribute__((packed));

struct ohci_regs {
    u32  revision;
    u32  control;
    u32  cmdstatus;
    u32  intrstatus;
    u32  intrenable;
    u32  intrdisable;
    u32  hcca;
    u32  ed_periodcurrent;
    u32  ed_controlhead;
    u32  ed_controlcurrent;
    u32  ed_bulkhead;
    u32  ed_bulkcurrent;
    u32  donehead;
    u32  fminterval;
    u32  fmremaining;
    u32  fmnumber;
    u32  periodicstart;
    u32  lsthresh;
    u32  roothub_a;
    u32  roothub_b;
    u32  roothub_status;
    u32  roothub_portstatus[15];
} __attribute__((packed));

/****************************************************************
 * Controller / pipe structures (SeaBIOS layout)
 ****************************************************************/
struct ohci_controller {
    struct usb_s usb;
    struct ohci_regs *regs;
    struct ohci_hcca *hcca;
    u8 bus;
    u8 dev;
    u8 func;
    u8 irq;
    u32 bar0;
    int ports;
};

struct ohci_pipe {
    struct ohci_ed ed;
    struct usb_pipe pipe;
    struct ohci_regs *regs;
    void *data;
    int count;
    struct ohci_td *tds;
};

/****************************************************************
 * Function declarations
 ****************************************************************/
u32 ohci_read32(struct ohci_regs *regs, u16 reg);
void ohci_write32(struct ohci_regs *regs, u16 reg, u32 val);
void ohci_setup(void);
struct usb_pipe *ohci_realloc_pipe(struct usbdevice_s *usbdev,
                                   struct usb_pipe *upipe,
                                   struct usb_endpoint_descriptor *epdesc);
int ohci_send_pipe(struct usb_pipe *p, int dir, const void *cmd,
                   void *data, int datasize);
int ohci_poll_intr(struct usb_pipe *p, void *data);

#endif
