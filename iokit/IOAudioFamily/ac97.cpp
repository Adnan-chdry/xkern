/*
 * ac97.cpp - AC97 audio codec driver (C++)
 *
 * Drives the Intel AC97 audio controller via PCI MMIO.
 * PCI class 04:01:00, NAM (Native Audio Mix) + NABM (Bus Master) BARs.
 */

#include "ac97.hpp"
#include "ac97.h"

extern "C" {
#include "IOPCIFamily/pci.h"
#include "paging.h"
#include "klog.h"
#include "klibc.h"
#include "devfs/devfs.h"
#include "io.h"
}

namespace ac97 {

    static Controller g_ctl;

    // ── MMIO access ────────────────────────────────────────────────

    u32 reg_read(void *base, u16 off) {
        return *(volatile u32 *)((uptr)base + off);
    }

    void reg_write(void *base, u16 off, u32 val) {
        *(volatile u32 *)((uptr)base + off) = val;
    }

    u16 nam_read(Controller *ctl, u16 reg) {
        return (u16)reg_read(ctl->nam_mmio, reg);
    }

    void nam_write(Controller *ctl, u16 reg, u16 val) {
        reg_write(ctl->nam_mmio, reg, val);
    }

    u32 nabm_read(Controller *ctl, u16 reg) {
        return reg_read(ctl->nabm_mmio, reg);
    }

    void nabm_write(Controller *ctl, u16 reg, u32 val) {
        reg_write(ctl->nabm_mmio, reg, val);
    }

    // ── Codec helpers ──────────────────────────────────────────────

    void codec_reset(Controller *ctl) {
        nam_write(ctl, NAM_RESET, 0xFFFF);

        for (volatile int i = 0; i < 100000; ++i)
            asm volatile("nop");

        u16 id1 = nam_read(ctl, NAM_VENDOR_ID1);
        u16 id2 = nam_read(ctl, NAM_VENDOR_ID2);
        ctl->vendor_id = id1;
        ctl->device_id = id2;

        klog("AC97", "codec ID 0x%04x:0x%04x", id1, id2);
    }

    void codec_dump(Controller *ctl) {
        u16 master  = nam_read(ctl, NAM_MASTER_VOL);
        u16 pcm     = nam_read(ctl, NAM_PCM_OUT_VOL);
        u16 mic     = nam_read(ctl, NAM_MIC_VOL);
        u16 rec_sel = nam_read(ctl, NAM_RECORD_SEL);
        u16 rec_gain= nam_read(ctl, NAM_RECORD_GAIN);
        u16 pwr     = nam_read(ctl, NAM_POWERDOWN);
        u32 ext_id  = nam_read(ctl, NAM_EXTENDED_ID);

        klog("AC97", "master vol 0x%04x  pcm out 0x%04x  mic 0x%04x",
             master, pcm, mic);
        klog("AC97", "rec sel 0x%04x  rec gain 0x%04x  pwr 0x%04x  ext_id 0x%04x",
             rec_sel, rec_gain, pwr, ext_id);
    }

    // ── BDL (Buffer Descriptor List) setup ─────────────────────────

    static void bdl_setup(Controller *ctl) {
        // Allocate BDL and sample buffer in low physical memory.
        // We reuse the paging allocator; these are kernel-virtual addresses
        // that we also know the physical address of via the paging API.
        ctl->bdl = (BufferDesc *)0;
        ctl->sample_buf = (u8 *)0;

        u64 bdl_vaddr = paging_alloc_and_map(0, PAGE_WRITE | PAGE_PRESENT);
        ctl->bdl_phys = (u32)bdl_vaddr;
        ctl->bdl = (BufferDesc *)bdl_vaddr;

        u64 sbuf_vaddr = paging_alloc_and_map(0, PAGE_WRITE | PAGE_PRESENT);
        ctl->sample_buf_phys = (u32)sbuf_vaddr;
        ctl->sample_buf = (u8 *)sbuf_vaddr;

        klibc.memset(ctl->bdl, 0, BDL_SIZE * sizeof(BufferDesc));
        klibc.memset(ctl->sample_buf, 0, SAMPLE_BUF_SIZE);

        klog("AC97", "BDL at 0x%x (phys), sample buf at 0x%x (phys)",
             ctl->bdl_phys, ctl->sample_buf_phys);
    }

    // ── Playback helpers ───────────────────────────────────────────

    static void pcm_out_start(Controller *ctl) {
        // Point the PCM OUT descriptor list pointer to our BDL.
        nabm_write(ctl, NABM_PCM_OUT_BASE, ctl->bdl_phys);

        // Reset the PCM OUT engine.
        nabm_write(ctl, NABM_PCM_OUT_CSR, PCM_CSR_RESET);

        for (volatile int i = 0; i < 10000; ++i)
            asm volatile("nop");

        // Clear any pending status bits.
        u32 csr = nabm_read(ctl, NABM_PCM_OUT_CSR);
        (void)csr;

        // Set last valid index to 0 (one buffer).
        u8 *lvi = (u8 *)((uptr)ctl->nabm_mmio + NABM_PCM_OUT_LVI);
        *lvi = 0;

        // Start playback.
        nabm_write(ctl, NABM_PCM_OUT_CSR,
                   PCM_CSR_START | PCM_CSR_IOC | PCM_CSR_FIFO_ERR);

        klog("AC97", "PCM OUT started");
    }

    static void pcm_out_stop(Controller *ctl) {
        u32 csr = nabm_read(ctl, NABM_PCM_OUT_CSR);
        csr &= ~PCM_CSR_START;
        nabm_write(ctl, NABM_PCM_OUT_CSR, csr);
        klog("AC97", "PCM OUT stopped");
    }

    // ── PCI probe & init ───────────────────────────────────────────

    static int pci_probe(Controller *ctl) {
        u8 bus = 0, dev = 0, func = 0;
        int found = pci_find_class(PCI_CLASS, PCI_SUBCLASS, PCI_PROGIF,
                                   &bus, &dev, &func);

        if (found != 0) {
            // Try wildcard progif.
            found = pci_find_class(PCI_CLASS, PCI_SUBCLASS, 0xFF,
                                   &bus, &dev, &func);
        }
        if (found != 0)
            return -1;

        ctl->bus  = bus;
        ctl->dev  = dev;
        ctl->func = func;

        u32 bar0 = pci_config_read(bus, dev, func, PCI_BAR0);
        u32 bar1 = pci_config_read(bus, dev, func, PCI_BAR1);

        // AC97 uses two MMIO BARs: BAR0 = NAM, BAR1 = NABM.
        // Both must be memory-mapped (bit 0 == 0).
        if ((bar0 & 1) || (bar1 & 1)) {
            klog("AC97", "unexpected I/O port BARs (0x%x, 0x%x)", bar0, bar1);
            return -1;
        }

        ctl->nam_bar  = bar0 & 0xFFFFFFF0;
        ctl->nabm_bar = bar1 & 0xFFFFFFF0;

        // Map both BAR regions into kernel virtual space.
        paging_map_region(ctl->nam_bar, ctl->nam_bar, 0x100,
                          PAGE_PRESENT | PAGE_WRITE);
        paging_map_region(ctl->nabm_bar, ctl->nabm_bar, 0x100,
                          PAGE_PRESENT | PAGE_WRITE);

        ctl->nam_mmio  = (void *)ctl->nam_bar;
        ctl->nabm_mmio = (void *)ctl->nabm_bar;

        // Read IRQ line.
        ctl->irq_line = pci_config_read_byte(bus, dev, func, PCI_INTERRUPT_LINE);

        klog("AC97", "PCI %d:%d.%d  NAM 0x%x  NABM 0x%x  IRQ %d",
             bus, dev, func, ctl->nam_bar, ctl->nabm_bar, ctl->irq_line);

        return 0;
    }

    // ── Public init / exit ─────────────────────────────────────────

    void dump(Controller *ctl) {
        codec_dump(ctl);
    }

} // namespace ac97

// C-linkage entry point called from io_storage_init().
int ac97_init(void) {
    klibc.memset(&ac97::g_ctl, 0, sizeof(ac97::g_ctl));

    if (ac97::pci_probe(&ac97::g_ctl) != 0) {
        klog("AC97", "no AC97 controller found");
        return -1;
    }

    // Power up the analog mixers and DAC.
    u16 pwr = ac97::nam_read(&ac97::g_ctl, ac97::NAM_POWERDOWN);
    pwr &= ~(ac97::PWR_ANALOG | ac97::PWR_DAC | ac97::PWR_ADC |
             ac97::PWR_MIXER | ac97::PWR_REF);
    ac97::nam_write(&ac97::g_ctl, ac97::NAM_POWERDOWN, pwr);

    // Reset the codec.
    ac97::codec_reset(&ac97::g_ctl);

    // Unmute master volume (both channels = 0).
    ac97::nam_write(&ac97::g_ctl, ac97::NAM_MASTER_VOL, 0x0000);
    // Unmute PCM out.
    ac97::nam_write(&ac97::g_ctl, ac97::NAM_PCM_OUT_VOL, 0x0000);
    // Set recording to mic.
    ac97::nam_write(&ac97::g_ctl, ac97::NAM_RECORD_SEL, 0x0001);
    // Mic gain +20 dB.
    ac97::nam_write(&ac97::g_ctl, ac97::NAM_MIC_GAIN, 0x0001);

    // Set standard 44100 Hz sample rate on playback.
    ac97::nam_write(&ac97::g_ctl, ac97::NAM_PCM_FRONT_DAC_RATE, 44100);

    // Dump codec state.
    ac97::codec_dump(&ac97::g_ctl);

    // Allocate BDL and sample buffer.
    ac97::bdl_setup(&ac97::g_ctl);

    // Prepare BDL entry 0: play silence initially.
    ac97::BufferDesc *bd = &ac97::g_ctl.bdl[0];
    bd->phys_addr = ac97::g_ctl.sample_buf_phys;
    bd->count     = 0;
    bd->flags     = 0;

    // Register as a character device in devfs.
    struct devfs_device ddev;
    klibc.memset(&ddev, 0, sizeof(ddev));
    klibc.snprintf(ddev.name, sizeof(ddev.name), "ac97");
    ddev.type = DEVFS_CHAR_DEV;
    ddev.block_size = 0;
    ddev.block_count = 0;
    klibc.snprintf(ddev.model, sizeof(ddev.model),
                   "AC97 0x%x:0x%x",
                   ac97::g_ctl.vendor_id,
                   ac97::g_ctl.device_id);
    ddev.priv = 0;
    ddev.read = 0;
    ddev.write = 0;
    devfs_register(&ddev);

    ac97::g_ctl.present = 1;

    klog("AC97", "init done");
    return 0;
}
