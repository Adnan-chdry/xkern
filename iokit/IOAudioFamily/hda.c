#include "hda.h"
#include "IOPCIFamily/pci.h"
#include "io.h"
#include "stdio.h"
#include "string.h"
#include "devfs/devfs.h"
#include "paging.h"
#include "klog.h"
#include "klibc.h"

static struct hda_controller g_hda_ctl;

static u32 hda_reg_read(struct hda_controller *ctl, u16 reg) {
    return *(volatile u32 *)((unsigned long)ctl->mmio + reg);
}

static void hda_reg_write(struct hda_controller *ctl, u16 reg, u32 val) {
    *(volatile u32 *)((unsigned long)ctl->mmio + reg) = val;
}

static u32 hda_verb_get(struct hda_controller *ctl, u8 codec, u16 node,
                        u16 verb) {
    u32 cmd = (codec << 28) | (node << 20) | verb;
    u32 wp = ctl->corb_wp;
    ctl->corb[wp] = cmd;
    wp = (wp + 1) % HDA_CORB_SIZE;
    ctl->corb_wp = wp;
    hda_reg_write(ctl, HDA_CORBBASE + HDA_CORBWP, wp);

    for (int i = 0; i < 100000; i++) {
        u32 rwp = hda_reg_read(ctl, HDA_CORBBASE + HDA_CORBRP) & 0xFF;
        if (rwp == wp)
            break;
    }

    for (int i = 0; i < 100000; i++) {
        u32 rirb_wp = hda_reg_read(ctl, HDA_RIRBBASE + HDA_RIRBWP) & 0xFF;
        if (rirb_wp != ctl->rirb_wp) {
            ctl->rirb_wp = rirb_wp;
            u32 resp = ctl->rirb[rirb_wp];
            u32 resp_ex = ctl->rirb[rirb_wp + 1];
            (void)resp_ex;
            return resp;
        }
    }

    return 0;
}

static void hda_codec_init(struct hda_controller *ctl, u8 addr) {
    struct hda_codec codec;
    klibc.memset(&codec, 0, sizeof(codec));
    codec.addr = addr;

    u32 vendor = hda_verb_get(ctl, addr, 0, HDA_VERB_GET_PARAMETER | HDA_PARAM_VENDOR_ID);
    if (vendor == 0 || vendor == 0xFFFFFFFF)
        return;

    codec.vendor_id = vendor & 0xFFFF;
    codec.device_id = (vendor >> 16) & 0xFFFF;
    codec.revision = hda_verb_get(ctl, addr, 0, HDA_VERB_GET_PARAMETER | HDA_PARAM_REVISION);

    u32 sub = hda_verb_get(ctl, addr, 0, HDA_VERB_GET_PARAMETER | HDA_PARAM_SUBORDINATE_COUNT);
    codec.start_node = sub & 0xFF;
    codec.end_node = (sub >> 16) & 0xFF;

    u32 fg = hda_verb_get(ctl, addr, 0, HDA_VERB_GET_PARAMETER | HDA_PARAM_FUNCTION_GROUP);
    codec.function_group = fg;

    codec.present = 1;

    klog("HDA", "codec %d vendor 0x%x device 0x%x rev 0x%x nodes %d-%d",
         addr, codec.vendor_id, codec.device_id,
         codec.revision, codec.start_node, codec.end_node);

    for (int nid = 2; nid <= codec.end_node && nid < HDA_MAX_WIDGETS; nid++) {
        u32 wcap = hda_verb_get(ctl, addr, nid,
                                HDA_VERB_GET_PARAMETER | HDA_PARAM_AUDIO_WIDGET);
        if (wcap == 0 || wcap == 0xFFFFFFFF)
            continue;
        u8 type = (wcap >> 20) & 0x0F;
        klog("HDA", "  node %d type %d caps 0x%x", nid, type, wcap);
    }

    struct devfs_device ddev;
    char dname[DEVFS_NAME_MAX];
    klibc.snprintf(dname, sizeof(dname), "hda%d", addr);
    klibc.snprintf(ddev.name, sizeof(ddev.name), "%s", dname);
    ddev.type = DEVFS_CHAR_DEV;
    ddev.block_size = 0;
    ddev.block_count = 0;
    klibc.snprintf(ddev.model, sizeof(ddev.model), "HDA codec 0x%x:0x%x",
             codec.vendor_id, codec.device_id);
    ddev.priv = 0;
    ddev.read = 0;
    ddev.write = 0;
    devfs_register(&ddev);
    klog("HDA", "%s registered", dname);
}

int hda_init(void) {
    klibc.memset(&g_hda_ctl, 0, sizeof(g_hda_ctl));

    u8 bus = 0, dev = 0, func = 0;
    int found = pci_find_class(HDA_PCI_CLASS, HDA_PCI_SUBCLASS, HDA_PCI_PROGIF,
                               &bus, &dev, &func);
    if (found != 0)
        found = pci_find_class(HDA_PCI_CLASS, 0x00, 0x00, &bus, &dev, &func);
    if (found != 0) {
        klog("HDA", "no HDA controller found");
        return -1;
    }

    u32 bar = pci_config_read(bus, dev, func, PCI_BAR0);
    g_hda_ctl.bar0 = bar & 0xFFFFFFF0;
    g_hda_ctl.mmio = (void *)(unsigned long)g_hda_ctl.bar0;
    g_hda_ctl.bus = bus;
    g_hda_ctl.dev = dev;
    g_hda_ctl.func = func;

    paging_map_region(g_hda_ctl.bar0, g_hda_ctl.bar0, 0x4000,
                      PAGE_PRESENT | PAGE_WRITE);

    klog("HDA", "controller at PCI %d:%d.%d BAR0 0x%x",
         bus, dev, func, g_hda_ctl.bar0);

    u16 gcap = (u16)hda_reg_read(&g_hda_ctl, HDA_GCAP);
    g_hda_ctl.gcap = gcap;
    g_hda_ctl.vmin = (u8)hda_reg_read(&g_hda_ctl, HDA_VMIN);
    g_hda_ctl.vmaj = (u8)hda_reg_read(&g_hda_ctl, HDA_VMAJ);
    g_hda_ctl.outpay = (u16)hda_reg_read(&g_hda_ctl, HDA_OUTPAY);
    g_hda_ctl.inpay = (u16)hda_reg_read(&g_hda_ctl, HDA_INPAY);

    klog("HDA", "GCAP 0x%x ver %d.%d oss %d iss %d",
         gcap, g_hda_ctl.vmaj, g_hda_ctl.vmin,
         g_hda_ctl.outpay, g_hda_ctl.inpay);

    u32 gctl = hda_reg_read(&g_hda_ctl, HDA_GCTL);
    gctl &= ~HDA_GCTL_CRST;
    hda_reg_write(&g_hda_ctl, HDA_GCTL, gctl);

    for (int i = 0; i < 1000; i++) {
        if (!(hda_reg_read(&g_hda_ctl, HDA_GCTL) & HDA_GCTL_CRST))
            break;
    }

    gctl |= HDA_GCTL_CRST;
    hda_reg_write(&g_hda_ctl, HDA_GCTL, gctl);

    for (int i = 0; i < 1000; i++) {
        if (hda_reg_read(&g_hda_ctl, HDA_GCTL) & HDA_GCTL_CRST)
            break;
    }

    hda_reg_write(&g_hda_ctl, HDA_WAKEEN, 0x7FFF);

    u32 dp_lbase = (u32)(unsigned long)g_hda_ctl.mmio + 0x1000;
    hda_reg_write(&g_hda_ctl, HDA_DPLBASE, dp_lbase | 1);
    hda_reg_write(&g_hda_ctl, HDA_DPUBASE, 0);

    g_hda_ctl.corb = (u32 *)(unsigned long)(g_hda_ctl.bar0 + 0x2000);
    g_hda_ctl.rirb = (u32 *)(unsigned long)(g_hda_ctl.bar0 + 0x3000);

    klibc.memset((void *)(unsigned long)g_hda_ctl.corb, 0, 0x1000);
    klibc.memset((void *)(unsigned long)g_hda_ctl.rirb, 0, 0x1000);
    g_hda_ctl.corb_wp = 0;
    g_hda_ctl.rirb_wp = 0;

    hda_reg_write(&g_hda_ctl, HDA_CORBBASE + 0x00, (u32)(unsigned long)g_hda_ctl.corb | 1);
    hda_reg_write(&g_hda_ctl, HDA_CORBBASE + HDA_CORBRP, 0x8000);
    hda_reg_write(&g_hda_ctl, HDA_CORBBASE + HDA_CORBRP, 0);
    hda_reg_write(&g_hda_ctl, HDA_CORBBASE + HDA_CORBWP, 0);
    hda_reg_write(&g_hda_ctl, HDA_CORBBASE + HDA_CORBSIZE, 0);
    hda_reg_write(&g_hda_ctl, HDA_CORBBASE + HDA_CORBCTL, HDA_CORBCTL_RUN);

    hda_reg_write(&g_hda_ctl, HDA_RIRBBASE + 0x00, (u32)(unsigned long)g_hda_ctl.rirb | 1);
    hda_reg_write(&g_hda_ctl, HDA_RIRBBASE + HDA_RIRBWP, 0x8000);
    hda_reg_write(&g_hda_ctl, HDA_RIRBBASE + HDA_RIRBWP, 0);
    hda_reg_write(&g_hda_ctl, HDA_RIRBBASE + HDA_RIRBSIZE, 0);
    hda_reg_write(&g_hda_ctl, HDA_RIRBBASE + HDA_RIRBCTL, HDA_RIRBCTL_RUN | HDA_RIRBCTL_OIC);

    for (int tries = 0; tries < 100; tries++) {
        u32 statests = hda_reg_read(&g_hda_ctl, HDA_STATESTS);
        if (statests)
            break;
    }

    u32 statests = hda_reg_read(&g_hda_ctl, HDA_STATESTS);
    klog("HDA", "STATESTS 0x%x", statests);

    for (u8 addr = 0; addr < HDA_MAX_CODECS; addr++) {
        if (statests & (1 << addr))
            hda_codec_init(&g_hda_ctl, addr);
    }

    if (!statests) {
        klog("HDA", "STATESTS=0, probing codec 0 anyway");
        hda_codec_init(&g_hda_ctl, 0);
    }

    hda_reg_write(&g_hda_ctl, HDA_STATESTS, statests);

    g_hda_ctl.found = 1;

    struct devfs_device ddev;
    klibc.snprintf(ddev.name, sizeof(ddev.name), "hda");
    ddev.type = DEVFS_CHAR_DEV;
    ddev.block_size = 0;
    ddev.block_count = 0;
    klibc.snprintf(ddev.model, sizeof(ddev.model), "HDA controller");
    ddev.priv = 0;
    ddev.read = 0;
    ddev.write = 0;
    devfs_register(&ddev);

    klog("HDA", "init done");
    return 0;
}
