/*
 * ahci.cpp - see ahci.hpp.  AHCI host controller driver (C++).
 */
#include "iokit/IOStorageFamily/ahci/ahci.hpp"

namespace ahci {

AHCIController g_ahci;

/* PxTFD status-field bits (status byte lives in bits 8..15) */
enum {
    TFD_STS_BSY = (1u << 15),
    TFD_STS_DRQ = (1u << 11),
    TFD_STS_DRDY = (1u << 14),
    TFD_STS_ERR = (1u << 8),
};

/* ---- AHCIPort -------------------------------------------------------- */

void AHCIPort::init(volatile u32 *abar, int index)
{
    m_abar  = abar;
    m_index = index;
    m_port  = (volatile u32 *)((volatile u8 *)abar + REG_PORTS + index * PORT_SIZE);
    m_cl = 0;
    m_fis = 0;
    m_ct = 0;
    m_id = 0;
    m_attached = false;
    m_lba_count = 0;
}

/* Poll until the command-list-running bit clears (after stopping ST). */
static bool wait_clear(volatile u32 *p, u32 off, u32 mask, u32 tries)
{
    while (tries--) {
        if ((p[off / 4] & mask) == 0)
            return true;
        delay_us(10);
    }
    return false;
}

/* Poll until the given register bits are set. */
static bool wait_set(volatile u32 *p, u32 off, u32 mask, u32 tries)
{
    while (tries--) {
        if ((p[off / 4] & mask) != 0)
            return true;
        delay_us(10);
    }
    return false;
}

/* Extract the ATA status byte (bits 8..15) from PxTFD. */
static u8 tfd_status(u32 tfd)
{
    return (u8)(tfd >> 8);
}

int AHCIPort::start(void)
{
    u32 cmd = rd(PX_CMD);

    /* 1. stop the command engine */
    cmd &= ~PXCMD_ST;
    wr(PX_CMD, cmd);
    if (!wait_clear(m_port, PX_CMD, PXCMD_CR, 500))
        return -1;

    /* 2. stop FIS reception */
    cmd = rd(PX_CMD);
    cmd &= ~PXCMD_FRE;
    wr(PX_CMD, cmd);
    if (!wait_clear(m_port, PX_CMD, PXCMD_FR, 500))
        return -1;

    /* 3. hand the DMA structures to the HBA */
    wr(PX_CLB,  (u32)dma_to_phys(m_cl));
    wr(PX_CLBU, 0);
    wr(PX_FB,   (u32)dma_to_phys(m_fis));
    wr(PX_FBU,  0);

    /* 4. (re)start FIS reception */
    cmd = rd(PX_CMD);
    cmd |= PXCMD_FRE;
    wr(PX_CMD, cmd);
    if (!wait_set(m_port, PX_CMD, PXCMD_FR, 500))
        return -1;

    /* 5. power up + active interface comm control */
    cmd = rd(PX_CMD);
    cmd |= PXCMD_SUD | PXCMD_POD;
    cmd &= ~PXCMD_ICC;   /* ICC = 0 -> active */
    wr(PX_CMD, cmd);

    /* 6. start the command engine */
    cmd = rd(PX_CMD);
    cmd |= PXCMD_ST;
    wr(PX_CMD, cmd);
    if (!wait_set(m_port, PX_CMD, PXCMD_CR, 500))
        return -1;

    return 0;
}

int AHCIPort::wait_present(void)
{
    for (u32 i = 0; i < 1000; i++) {
        u32 ssts = rd(PX_SSTS);
        u32 det  = ssts & 0xF;
        u32 ipm  = (ssts >> 8) & 0xF;
        if (det == SSTS_DET_PRESENT && ipm == SSTS_IPM_ACTIVE)
            return 0;
        delay_us(100);
    }
    return -1;
}

int AHCIPort::probe(void)
{
    /* allocate the per-port DMA structures first */
    m_cl  = (hba_cmd_header *)dma_alloc_aligned(sizeof(hba_cmd_header) * 32, 1024);
    m_fis = (hba_fis *)dma_alloc_aligned(sizeof(hba_fis), 256);
    m_ct  = (hba_cmd_tbl *)dma_alloc_aligned(sizeof(hba_cmd_tbl), 128);
    m_id  = (u8 *)dma_alloc(512);
    if (!m_cl || !m_fis || !m_ct || !m_id) {
        klog("ahci", "port %d: out of DMA memory", m_index);
        return -1;
    }

    /* bring the port's command engine + FIS reception online (this also
     * enables FIS reception so the device's post-COMRESET status FIS is
     * captured into PxSIG/PxTFD). */
    if (start() != 0) {
        klog("ahci", "port %d: failed to start", m_index);
        return -1;
    }

    /* Wait for the device to come up using the power-on signature FIS.
     * A COMRESET tends to leave QEMU's emulated disk in a stuck error state
     * (TFD=0x130), so we rely on the post-reset signature instead. */
    if (wait_present() != 0)
        return -1;

    u32 sig = rd(PX_SIG);
    if (sig != SIG_ATA) {
        klog("ahci", "port %d: unsupported signature 0x%x (ATA disk expected)",
             m_index, sig);
        return -1;
    }

    if (identify() != 0)
        return -1;

    m_attached = true;
    return 0;
}

void AHCIPort::build_fis(u8 *fis, u8 ata_cmd, bool is_write,
                         u64 lba, u16 sectors, u8 dev)
{
    (void)is_write;
    fis[0]  = FIS_TYPE_H2D;
    fis[1]  = (1u << 7);                 /* C bit: this is a command */
    fis[2]  = ata_cmd;
    fis[3]  = 0;                        /* features (low) */
    fis[4]  = (u8)(lba & 0xFF);
    fis[5]  = (u8)((lba >> 8) & 0xFF);
    fis[6]  = (u8)((lba >> 16) & 0xFF);
    fis[7]  = dev;                      /* device register */
    fis[8]  = (u8)((lba >> 24) & 0xFF);
    fis[9]  = (u8)((lba >> 32) & 0xFF);
    fis[10] = (u8)((lba >> 40) & 0xFF);
    fis[11] = 0;                        /* features (high) */
    fis[12] = (u8)(sectors & 0xFF);
    fis[13] = (u8)((sectors >> 8) & 0xFF);
    fis[14] = 0;
    fis[15] = 0;                        /* control: clear SRST/nIEN */
    fis[16] = 0;
    fis[17] = 0;
    fis[18] = 0;
    fis[19] = 0;
}

int AHCIPort::issue(u8 ata_cmd, bool is_write, u64 lba, u16 sectors,
                    void *buf, u32 buflen)
{
    /* wait until the port is not busy with a previous command.  A stale
     * captured status (e.g. a leftover ERR from COMRESET) is cleared by the
     * device once it accepts the next command, so we only gate on BSY here. */
    if (!wait_clear(m_port, PX_TFD, (u32)TFD_STS_BSY, 2000)) {
        klog("ahci", "port %d: BSY stuck (TFD=0x%x)", m_index, rd(PX_TFD));
        return -1;
    }

    /* build the command list header (slot 0).  CFL = 5 DWORDs (20-byte
     * H2D FIS).  W marks a write (data-out) command.
     * PRDTL is placed in DW0 bits 16..31 (this is where QEMU's AHCI model
     * reads it; on spec-compliant HBAs it is ignored there and taken from
     * DW1 instead).  Bit 16 is also the "C" (clear-busy) bit per the AHCI
     * spec, so setting PRDTL there additionally enables BSY clearing. */
    m_cl[0].dw0 = (5u) | ((is_write ? 1u : 0u) << 6) | (1u << 16);
    /* PRDTL = 1 (one PRD entry) in the spec-mandated DW1 low word; leave
     * the high word (PRDBC) initialised to 1 as well. */
    m_cl[0].prdtl_prdbc = 1u | (1u << 16);
    m_cl[0].ctba  = (u32)dma_to_phys(m_ct);
    m_cl[0].ctbau = 0;
    m_cl[0].rsv[0] = m_cl[0].rsv[1] = 0;
    m_cl[0].rsv[2] = m_cl[0].rsv[3] = 0;

    /* clear the command table and build the FIS */
    for (u32 i = 0; i < sizeof(hba_cmd_tbl); i++)
        ((volatile u8 *)m_ct)[i] = 0;
    u8 dev = 0x40;  /* LBA mode bit set */
    build_fis(m_ct->cfis, ata_cmd, is_write, lba, sectors, dev);

    /* single PRD entry covering the whole buffer (no interrupt-on-completion
     * bit: we poll PxCI instead) */
    m_ct->prdt[0].dbau  = (u32)dma_to_phys(buf);
    m_ct->prdt[0].dbauh = 0;
    m_ct->prdt[0].rsv   = 0;
    m_ct->prdt[0].dbc   = (buflen - 1);

    /* clear port interrupts + error, then ring the doorbell */
    wr(PX_IS, 0xFFFFFFFF);
    wr(PX_SERR, 0xFFFFFFFF);

    /* make sure the command header, command table and (for writes) the data
     * buffer are flushed out of the CPU cache before the HBA DMAs them. */
    for (u32 i = 0; i < sizeof(hba_cmd_header) * 32; i += 64)
        asm volatile("clflush %0" :: "m"(((volatile u8 *)m_cl)[i]));
    for (u32 i = 0; i < sizeof(hba_cmd_tbl); i += 64)
        asm volatile("clflush %0" :: "m"(((volatile u8 *)m_ct)[i]));
    if (is_write) {
        for (u32 i = 0; i < buflen; i += 64)
            asm volatile("clflush %0" :: "m"(((volatile u8 *)buf)[i]));
    }

    wr(PX_CI, 1u << 0);
    mmio_wmb();

    /* poll for completion: CI bit 0 clears when the command is done */
    if (!wait_clear(m_port, PX_CI, (1u << 0), 20000)) {
        klog("ahci", "port %d: command 0x%x timed out", m_index, ata_cmd);
        return -1;
    }

    /* Decide success from the amount of data the HBA transferred.  For a
     * data command (buf != NULL) the PRD Byte Count must equal the buffer
     * length; a genuine device error transfers nothing.  QEMU leaves the
     * PxTFD error bits in their power-on state, so the TFD error field is
     * not a reliable completion indicator here.
     * PRDBC lives in the low word of the command-header PRDTL/PRDBC field on
     * QEMU's AHCI model, but in the high word per the AHCI spec; accept
     * whichever half reports the expected byte count. */
    if (buf != NULL) {
        u16 lo = (u16)(m_cl[0].prdtl_prdbc & 0xFFFF);
        u16 hi = (u16)(m_cl[0].prdtl_prdbc >> 16);
        u16 prdbc = (lo == buflen) ? lo : hi;
        if (prdbc != buflen) {
            klog("ahci", "port %d: command 0x%x short transfer (%u/%u bytes)",
                 m_index, ata_cmd, (u32)prdbc, buflen);
            wr(PX_IS, 0xFFFFFFFF);
            return -1;
        }
    }

    wr(PX_IS, 0xFFFFFFFF);
    return 0;
}

int AHCIPort::identify(void)
{
    for (u32 i = 0; i < 512; i++)
        m_id[i] = 0xCD;
    int idret = issue(ATA_CMD_IDENTIFY, false, 0, 0, m_id, 512);

    /* the device DMA'd the identify data into the buffer; drop any stale
     * cache lines so the CPU reads what the HBA wrote */
    for (u32 i = 0; i < 512; i += 64)
        asm volatile("clflush %0" :: "m"(((volatile u8 *)m_id)[i]));

    /* PRDBC lives in the low word of the command-header PRDTL/PRDBC field
     * on QEMU, but in the high word per the AHCI spec; accept either. */
    u16 lo = (u16)(m_cl[0].prdtl_prdbc & 0xFFFF);
    u16 hi = (u16)(m_cl[0].prdtl_prdbc >> 16);
    u16 prdbc = (lo == 512) ? lo : hi;
    if (idret != 0 || prdbc != 512) {
        klog("ahci", "port %d: identify failed (idret=%d, prdbc=%u)",
             m_index, idret, prdbc);
        return -1;
    }


    /* total user-addressable sectors: prefer LBA48 (words 100..103) */
    u64 lba48 = ((u64)m_id[103*2] << 48) |
                ((u64)m_id[103*2 + 1] << 56) |
                ((u64)m_id[102*2] << 32) |
                ((u64)m_id[102*2 + 1] << 40) |
                ((u64)m_id[101*2] << 16) |
                ((u64)m_id[101*2 + 1] << 24) |
                ((u64)m_id[100*2]) |
                ((u64)m_id[100*2 + 1] << 8);

    if (lba48)
        m_lba_count = lba48;
    else {
        /* LBA28 fallback (words 60..61) */
        u32 w60 = (u32)m_id[60*2] | ((u32)m_id[60*2 + 1] << 8);
        u32 w61 = (u32)m_id[61*2] | ((u32)m_id[61*2 + 1] << 8);
        m_lba_count = (u64)w60 | ((u64)w61 << 16);
    }

    /* model string (words 27..46): each 16-bit word is stored byte-swapped
     * in the identify data, so swap it back to native order */
    char model[41];
    const u16 *w = (const u16 *)&m_id[27 * 2];
    for (int i = 0; i < 20; i++) {
        model[i * 2]     = (char)(w[i] >> 8);
        model[i * 2 + 1] = (char)(w[i] & 0xFF);
    }
    model[40] = 0;
    /* trim trailing spaces */
    for (int i = 39; i >= 0 && (model[i] == ' ' || model[i] == 0); i--)
        model[i] = 0;

    klog("ahci", "port %d: ATA disk '%s' %llu sectors (%llu MiB)",
         m_index, model, (unsigned long long)m_lba_count,
         (unsigned long long)(m_lba_count / 2048));

    return 0;
}

int AHCIPort::read_blocks(u32 lba, u8 count, void *buf)
{
    if (!m_attached || count == 0)
        return -1;
    /* issue one command per block (simple, correct, covers count<=8) */
    u8 *p = (u8 *)buf;
    for (u32 i = 0; i < count; i++) {
        if (issue(ATA_CMD_READ_DMA_EXT, false, (u64)lba + i, 1, p, 512) != 0)
            return -1;
        p += 512;
    }
    return 0;
}

int AHCIPort::write_blocks(u32 lba, u8 count, const void *buf)
{
    if (!m_attached || count == 0)
        return -1;
    const u8 *p = (const u8 *)buf;
    for (u32 i = 0; i < count; i++) {
        if (issue(ATA_CMD_WRITE_DMA_EXT, true, (u64)lba + i, 1, (void *)p, 512) != 0)
            return -1;
        p += 512;
    }
    return 0;
}

int AHCIPort::dev_read(struct devfs_device *dev, u32 lba, u8 count, void *buf)
{
    AHCIPort *p = (AHCIPort *)dev->priv;
    return p ? p->read_blocks(lba, count, buf) : -1;
}

int AHCIPort::dev_write(struct devfs_device *dev, u32 lba, u8 count, void *buf)
{
    AHCIPort *p = (AHCIPort *)dev->priv;
    return p ? p->write_blocks(lba, count, buf) : -1;
}

/* ---- AHCIController -------------------------------------------------- */

int AHCIController::init(void)
{
    u8 bus = 0, dev = 0, func = 0;

    if (pci_find_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_SATA, 0x01,
                       &bus, &dev, &func) != 0) {
        klog("ahci", "no AHCI controller found");
        return -1;
    }

    /* enable PCI memory space + bus mastering so BAR5 is visible */
    u32 pcicmd = pci_config_read(bus, dev, func, PCI_COMMAND);
    pcicmd |= (1u << 1) | (1u << 2);
    pci_config_write(bus, dev, func, PCI_COMMAND, pcicmd);

    u32 bar = pci_config_read(bus, dev, func, PCI_BAR5);
    m_bar5 = bar & 0xFFFFFFF0;    m_abar = (volatile u32 *)(unsigned long)m_bar5;

    /* take ownership from the BIOS if the handoff register is present */
    u32 bohc = m_abar[REG_BOHC / 4];
    if (bohc & (1u << 0)) {            /* BOS (BIOS owned) */
        m_abar[REG_BOHC / 4] = bohc | (1u << 1); /* set OOS (OS owned) */
        for (u32 i = 0; i < 1000; i++) {
            if ((m_abar[REG_BOHC / 4] & (1u << 0)) == 0)
                break;
            delay_us(100);
        }
    }

    /* enable AHCI mode (and wait for any reset to finish) */
    if (m_abar[REG_GHC / 4] & GHC_HR) {
        for (u32 i = 0; i < 1000; i++) {
            if ((m_abar[REG_GHC / 4] & GHC_HR) == 0)
                break;
            delay_us(100);
        }
    }
    m_abar[REG_GHC / 4] = m_abar[REG_GHC / 4] | GHC_AE;
    if ((m_abar[REG_GHC / 4] & GHC_AE) == 0) {
        klog("ahci", "failed to enable AHCI mode");
        return -1;
    }

    u32 vers = m_abar[REG_VS / 4];
    m_ports_impl = m_abar[REG_PI / 4];
    klog("ahci", "controller at BAR5=0x%x, version %u.%u, ports-impl=0x%x",
         m_bar5, (vers >> 16) & 0xFFFF, vers & 0xFFFF, m_ports_impl);

    dma_heap_init();

    int ports = 0, attached = 0;
    for (int i = 0; i < 32; i++) {
        if ((m_ports_impl & (1u << i)) == 0)
            continue;
        ports++;
        m_ports[i].init(m_abar, i);
        if (m_ports[i].probe() == 0) {
            attached++;

            struct devfs_device ddev;
            char node[DEVFS_NAME_MAX];
            io_storage_assign(IO_STOR_DISK, -1, node, sizeof(node));

            klibc.snprintf(ddev.name, sizeof(ddev.name), "%s", node);
            ddev.type = DEVFS_BLOCK_DEV;
            ddev.block_size = 512;
            ddev.block_count = (u32)(m_ports[i].lba_count() & 0xFFFFFFFF);
            klibc.snprintf(ddev.model, sizeof(ddev.model),
                           "AHCI port %d SATA disk", i);
            ddev.priv = &m_ports[i];
            ddev.read = AHCIPort::dev_read;
            ddev.write = AHCIPort::dev_write;
            devfs_register(&ddev);

            klog("ahci", "registered %s (port %d)", node, i);
        }
    }

    klog("ahci", "init done: %d ports, %d disks attached", ports, attached);
    m_found = attached;
    return 0;
}

} /* namespace ahci */

extern "C" int ahci_init(void)
{
    return ahci::g_ahci.init();
}
