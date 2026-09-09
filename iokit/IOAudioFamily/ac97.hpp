#pragma once

#include "types.h"

namespace ac97 {

    // AC97 PCI class codes.
    constexpr u8 PCI_CLASS    = 0x04;
    constexpr u8 PCI_SUBCLASS = 0x01;
    constexpr u8 PCI_PROGIF   = 0x00;

    // Native Audio Mix registers (NAM / offset from NABM BAR).
    constexpr u16 NAM_RESET        = 0x00;
    constexpr u16 NAM_MASTER_VOL   = 0x02;
    constexpr u16 NAM_MASTER_MONO  = 0x06;
    constexpr u16 NAM_PCBEEP_VOL   = 0x0A;
    constexpr u16 NAM_PHONE_VOL    = 0x0C;
    constexpr u16 NAM_MIC_VOL      = 0x0E;
    constexpr u16 NAM_LINE_IN_VOL  = 0x10;
    constexpr u16 NAM_CD_VOL       = 0x12;
    constexpr u16 NAM_VIDEO_VOL    = 0x14;
    constexpr u16 NAM_AUX_VOL      = 0x16;
    constexpr u16 NAM_PCM_OUT_VOL  = 0x18;
    constexpr u16 NAM_RECORD_SEL   = 0x1A;
    constexpr u16 NAM_MIC_GAIN     = 0x20;
    constexpr u16 NAM_RECORD_GAIN  = 0x22;
    constexpr u16 NAM_EXTENDED_ID  = 0x28;
    constexpr u16 NAM_EXTENDED_STS = 0x2A;
    constexpr u16 NAM_POWERDOWN    = 0x2C;
    constexpr u16 NAM_EXTENDED_CFG = 0x2E;
    constexpr u16 NAM_PCM_FRONT_DAC_RATE = 0x2C;
    constexpr u16 NAM_PCM_SURR_DAC_RATE  = 0x2E;
    constexpr u16 NAM_PCM_LFE_DAC_RATE   = 0x30;
    constexpr u16 NAM_PCM_LR_ADC_RATE    = 0x32;
    constexpr u16 NAM_MIC_ADC_RATE       = 0x34;
    constexpr u16 NAM_VENDOR_ID1         = 0x7C;
    constexpr u16 NAM_VENDOR_ID2         = 0x7E;

    // NABM (Bus Master) registers (offset from NABM BAR).
    constexpr u16 NABM_PCM_OUT_BASE   = 0x10;
    constexpr u16 NABM_PCM_OUT_CSR    = 0x14;
    constexpr u16 NABM_PCM_OUT_LVI    = 0x15;
    constexpr u16 NABM_PCM_OUT_IV     = 0x16;
    constexpr u16 NABM_PCM_OUT_PICB   = 0x18;
    constexpr u16 NABM_PCM_OUT_PIVR   = 0x1A;
    constexpr u16 NABM_PCM_OUT_DESC_COUNT = 0x1B;
    constexpr u16 NABM_MIC_IN_BASE    = 0x20;
    constexpr u16 NABM_MIC_IN_CSR     = 0x24;
    constexpr u16 NABM_MIC_IN_LVI     = 0x25;
    constexpr u16 NABM_MIC_IN_IV      = 0x26;
    constexpr u16 NABM_MIC_IN_PICB    = 0x28;
    constexpr u16 NABM_MIC_IN_PIVR    = 0x2A;
    constexpr u16 NABM_MIC_IN_DESC_COUNT = 0x2B;
    constexpr u16 NABM_PO_GLOBAL_CTRL  = 0x2C;
    constexpr u16 NABM_PO_GLOBAL_STATUS = 0x30;
    constexpr u16 NABM_PO_LITERAL_STS  = 0x34;
    constexpr u16 NABM_BUFFER_DESC_FMT = 0x38;

    // PCM OUT CSR bits.
    constexpr u8 PCM_CSR_RESET       = 0x02;
    constexpr u8 PCM_CSR_START       = 0x01;
    constexpr u8 PCM_CSR_IOC         = 0x04;
    constexpr u8 PCM_CSR_FIFO_ERR    = 0x08;
    constexpr u8 PCM_CSR_DMA_ERR     = 0x10;

    // Power-down register bits.
    constexpr u16 PWR_ANALOG          = 0x0001;
    constexpr u16 PWR_DAC             = 0x0200;
    constexpr u16 PWR_ADC             = 0x0400;
    constexpr u16 PWR_MIXER          = 0x0800;
    constexpr u16 PWR_REF            = 0x1000;

    // Buffer descriptor entry (32 bytes aligned).
    struct BufferDesc {
        u32 phys_addr;
        u16 count;       // bits 0-13: sample count, bit 14: IOC, bit 15: reserved
        u16 flags;       // bit 15: BUP (buffer underrun potion)
    };

    // BDL (Buffer Descriptor List) sizes.
    constexpr u32 BDL_SIZE  = 32;      // 32 entries max
    constexpr u32 SAMPLE_BUF_SIZE = 0x1000; // 4 KiB per buffer descriptor

    // Driver state.
    struct Controller {
        u8   bus;
        u8   dev;
        u8   func;
        u32  nam_bar;        // Native Audio Mix (NAM) MMIO base
        u32  nabm_bar;       // Bus Master (NABM) MMIO base
        void *nam_mmio;
        void *nabm_mmio;
        u16  vendor_id;
        u16  device_id;
        u8   irq_line;
        int  present;

        // Playback buffer descriptors (allocated in low memory).
        BufferDesc *bdl;
        u32        bdl_phys;
        u8         *sample_buf;
        u32        sample_buf_phys;
    };

    // Volatile MMIO helpers.
    u32  reg_read(void *base, u16 off);
    void reg_write(void *base, u16 off, u32 val);
    u16  nam_read(Controller *ctl, u16 reg);
    void nam_write(Controller *ctl, u16 reg, u16 val);
    u32  nabm_read(Controller *ctl, u16 reg);
    void nabm_write(Controller *ctl, u16 reg, u32 val);

    // Codec identification.
    void codec_reset(Controller *ctl);
    void codec_dump(Controller *ctl);

} // namespace ac97
