#pragma once
/*
 * ahci.hpp - AHCI (SATA) host controller driver, C++ edition.
 *
 * A from-scratch, polling (no interrupts) AHCI 1.x driver written in the
 * kernel's freestanding C++20 subset (no RTTI, no exceptions, no libstdc++).
 * It follows the IOKit-ish layering used elsewhere in IOStorageFamily:
 *
 *     AHCIController  - one per PCI AHCI HBA; enumerates implemented ports
 *     AHCIPort        - one per port; owns the DMA command structures and
 *                       performs identify / read / write, then registers a
 *                       block device with devfs.
 *
 * DMA buffers come from the helper in dynamic_util.cpp (ahci::dma_alloc*).
 */
#include "types.h"
#include <cstddef>

extern "C" {
#include "klog.h"
#include "string.h"
#include "klibc.h"
#include "iokit/IOStorageFamily/ahci/pci_cxx.hpp"
#include "devfs/devfs.h"
#include "IOStorageFamily/io_storage.h"
}

#include "iokit/IOStorageFamily/ahci/dynamic_util.hpp"

namespace ahci {

/* ---- Generic Host Control register offsets (from BAR5) -------------- */
enum {
    REG_CAP   = 0x00,  /* HBA capabilities                */
    REG_GHC   = 0x04,  /* global HBA control              */
    REG_IS    = 0x08,  /* interrupt status                */
    REG_PI    = 0x0C,  /* ports implemented               */
    REG_VS    = 0x10,  /* AHCI version                    */
    REG_CAP2  = 0x24,  /* HBA capabilities extended        */
    REG_BOHC  = 0x28,  /* BIOS/OS handoff control         */
    REG_PORTS = 0x100, /* first port register block       */
    PORT_SIZE = 0x80,  /* bytes per port                  */
};

/* GHC bits */
enum {
    GHC_AE = (1u << 31),  /* AHCI enable                     */
    GHC_HR = (1u << 0),   /* HBA reset                       */
};

/* Port register offsets (within a port block) */
enum {
    PX_CLB  = 0x00,  /* command list base (low)         */
    PX_CLBU = 0x04,  /* command list base (high)        */
    PX_FB   = 0x08,  /* FIS base (low)                  */
    PX_FBU  = 0x0C,  /* FIS base (high)                 */
    PX_IS   = 0x10,  /* port interrupt status           */
    PX_IE   = 0x14,  /* port interrupt enable           */
    PX_CMD  = 0x18,  /* port command                    */
    PX_TFD  = 0x20,  /* task file data                  */
    PX_SIG  = 0x24,  /* device signature                */
    PX_SSTS = 0x28,  /* SATA status                     */
    PX_SCTL = 0x2C,  /* SATA control                    */
    PX_SERR = 0x30,  /* SATA error                      */
    PX_CI   = 0x38,  /* command issue                   */
    PX_SACT = 0x3C,  /* SATA active                     */
};

/* PX_CMD bits */
enum {
    PXCMD_ST    = (1u << 0),   /* start                          */
    PXCMD_SUD   = (1u << 1),   /* spin up device                 */
    PXCMD_POD   = (1u << 2),   /* power on device                */
    PXCMD_FRE   = (1u << 4),   /* FIS receive enable             */
    PXCMD_FR    = (1u << 14),  /* FIS receive running            */
    PXCMD_CR    = (1u << 15),  /* command list running           */
    PXCMD_ICC   = (0xFu << 28),/* interface comm control         */
};

/* PX_SSTS fields */
enum {
    SSTS_DET_PRESENT = 3,  /* device present + comm established */
    SSTS_IPM_ACTIVE  = 1,  /* interface active                  */
};

/* PX_TFD bits (task file) */
enum {
    TFD_ERR = (1u << 0),  /* earlier bit; we read ERR/BSY/DRQ below */
    TFD_BSY = (1u << 7),
    TFD_DRQ = (1u << 3),
    TFD_ERR_BIT = (1u << 0),
};

/* Device signatures (PX_SIG) */
enum {
    SIG_ATA    = 0x00000101,  /* SATA hard disk                */
    SIG_ATAPI  = 0xEB140101,  /* SATAPI packet device          */
    SIG_SEMB   = 0xC33C0101,  /* enclosure management bridge   */
    SIG_PM     = 0x96690101,  /* port multiplier               */
};

/* FIS types */
enum {
    FIS_TYPE_H2D = 0x27,  /* host-to-device register FIS   */
};

/* ATA commands (LBA48 variants) */
enum {
    ATA_CMD_IDENTIFY      = 0xEC,
    ATA_CMD_READ_DMA_EXT  = 0x25,
    ATA_CMD_WRITE_DMA_EXT = 0x35,
};

/* ---- DMA structures (must match the AHCI spec layouts) --------------- */

struct hba_cmd_header {
    u32 dw0;            /* CFL/A/W/P/R/C/PMP + rsv */
    u32 prdtl_prdbc;    /* PRDTL (low16), PRD byte count (high16) */
    u32 ctba;           /* command table base low */
    u32 ctbau;          /* command table base high */
    u32 rsv[4];
};

struct hba_prdt {
    u32 dbau;           /* data base low */
    u32 dbauh;          /* data base high */
    u32 rsv;
    u32 dbc;            /* bit31 I, bits0-21 byte count (0-based) */
};

struct hba_cmd_tbl {
    u8  cfis[64];       /* command FIS (H2D occupies 20 bytes) */
    u8  acmd[16];       /* ATAPI command */
    u8  rsv[48];
    hba_prdt prdt[8];   /* up to 8 PRD entries */
};

struct hba_fis {
    u8 bytes[256];      /* received FIS */
};

/* ---- Port ------------------------------------------------------------ */

class AHCIPort {
public:
    void init(volatile u32 *abar, int index);

    int  probe(void);
    int  identify(void);

    /* devfs callbacks (free functions that dispatch to the instance) */
    int  read_blocks(u32 lba, u8 count, void *buf);
    int  write_blocks(u32 lba, u8 count, const void *buf);

    bool attached(void) const { return m_attached; }
    u64  lba_count(void) const { return m_lba_count; }
    int  index(void) const { return m_index; }

    /* C-callable devfs dispatch trampolines */
    static int dev_read(struct devfs_device *dev, u32 lba, u8 count, void *buf);
    static int dev_write(struct devfs_device *dev, u32 lba, u8 count, void *buf);

private:
    int  start(void);
    int  wait_present(void);
    int  issue(u8 ata_cmd, bool is_write, u64 lba, u16 sectors,
               void *buf, u32 buflen);
    void build_fis(u8 *fis, u8 ata_cmd, bool is_write, u64 lba, u16 sectors, u8 dev);

    volatile u32 *m_abar;   /* host base (BAR5) */
    int           m_index;
    volatile u32 *m_port;   /* this port's register block */

    hba_cmd_header *m_cl;   /* command list (1 KiB aligned) */
    hba_fis        *m_fis;  /* received FIS (256B aligned) */
    hba_cmd_tbl    *m_ct;   /* command table */
    u8             *m_id;   /* identify buffer (512B) */

    bool m_attached;
    u64  m_lba_count;

    u32  rd(u32 off) const { return m_port[off / 4]; }
    void wr(u32 off, u32 v) { m_port[off / 4] = v; }
};

/* ---- Controller ------------------------------------------------------ */

class AHCIController {
public:
    int init(void);

private:
    u32          m_bar5;
    volatile u32 *m_abar;
    u32          m_ports_impl;
    int          m_found;

    AHCIPort     m_ports[32];
};

/* The single controller instance driven by ahci_init(). */
extern AHCIController g_ahci;

}

/* C entry point so the existing io_storage_init() call keeps working. */
extern "C" int ahci_init(void);
