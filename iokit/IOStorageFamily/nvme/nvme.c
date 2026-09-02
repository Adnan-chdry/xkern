#include "klog.h"
#include "types.h"
#include "io.h"
#include "IOPCIFamily/pci.h"
#include "stdio.h"
#include "devfs/devfs.h"
#include "string.h"
#include "klibc.h"

#define NVME_PCI_CLASS    0x01
#define NVME_PCI_SUBCLASS 0x08
#define NVME_PCI_PROGIF   0x02

#define CAP_REG_OFFSET    0x0000
#define VS_REG_OFFSET     0x0008
#define INTMS_REG_OFFSET  0x000C
#define INTMC_REG_OFFSET  0x0010
#define CC_REG_OFFSET     0x0014
#define CSTS_REG_OFFSET   0x001C
#define AQA_REG_OFFSET    0x0024
#define ASQ_REG_OFFSET    0x0028
#define ACQ_REG_OFFSET    0x0030

#define CC_EN     (1 << 0)
#define CSTS_RDY  (1 << 0)

static int nvme_found;
static u32 nvme_bar0;

static u32 nvme_read_reg(u32 offset) {
    return *(volatile u32 *)(unsigned long)(nvme_bar0 + offset);
}

static void nvme_write_reg(u32 offset, u32 val) {
    *(volatile u32 *)(unsigned long)(nvme_bar0 + offset) = val;
}

int nvme_init(void) {
    u8 bus = 0, dev = 0, func = 0;

    if (pci_find_class(NVME_PCI_CLASS, NVME_PCI_SUBCLASS, NVME_PCI_PROGIF, &bus, &dev, &func) != 0) {
        klog("nvme", "no NVMe controller found");
        return -1;
    }

    u32 bar = pci_config_read(bus, dev, func, 0x10);
    nvme_bar0 = bar & 0xFFFFFFF0;

    u32 cap = nvme_read_reg(CAP_REG_OFFSET);
    u32 vs = nvme_read_reg(VS_REG_OFFSET);

    klog("NVMe", "BAR0=0x%x, CAP=0x%x, VS=0x%x", nvme_bar0, cap, vs);

    u32 cc = nvme_read_reg(CC_REG_OFFSET);
    cc |= CC_EN;
    nvme_write_reg(CC_REG_OFFSET, cc);

    for (int i = 0; i < 1000; i++) {
        if (nvme_read_reg(CSTS_REG_OFFSET) & CSTS_RDY)
            break;
    }

    nvme_found = 1;

    struct devfs_device ddev;
    klibc.snprintf(ddev.name, sizeof(ddev.name), "nvme0");
    ddev.type = DEVFS_BLOCK_DEV;
    ddev.block_size = 512;
    ddev.block_count = 0;
    klibc.snprintf(ddev.model, sizeof(ddev.model), "NVMe controller");
    ddev.priv = 0;
    ddev.read = 0;
    ddev.write = 0;
    devfs_register(&ddev);

    klog("nvme", "nvme_init() done");
    return 0;
}
