#include "klog.h"
#include "types.h"
#include "io.h"
#include "IOPCIFamily/pci.h"
#include "stdio.h"
#include "devfs/devfs.h"
#include "string.h"
#include "klibc.h"

#define AHCI_PCI_CLASS     0x01
#define AHCI_PCI_SUBCLASS  0x06
#define AHCI_PCI_PROGIF    0x01

#define HBA_GHC    0x0000
#define HBA_GHC_AE (1 << 31)
#define HBA_GHC_HR (1 << 0)

#define HBA_CAP    0x0000
#define HBA_PI     0x000C

#define PORT_CMD_ST  (1 << 0)
#define PORT_CMD_FRE (1 << 4)
#define PORT_CMD_FR  (1 << 14)
#define PORT_CMD_CR  (1 << 15)

static u32 ahci_bar5;
static volatile u32 *ahci_ghc;
static int ahci_found;

int ahci_init(void) {
    u8 bus, dev, func;
    u32 bar;

    if (pci_find_class(AHCI_PCI_CLASS, AHCI_PCI_SUBCLASS, AHCI_PCI_PROGIF, &bus, &dev, &func) != 0) {
        klog("ahci", "no AHCI controller found");
        return -1;
    }

    bar = pci_config_read(bus, dev, func, 0x24);
    ahci_bar5 = bar & 0xFFFFFFF0;
    ahci_ghc = (volatile u32 *)(unsigned long)(ahci_bar5 + HBA_GHC);

    ahci_ghc[0] |= HBA_GHC_AE;

    u32 pi = *(volatile u32 *)(unsigned long)(ahci_bar5 + HBA_PI);
    int ports = 0;
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i))
            ports++;
    }

    klog("AHCI", "AHCI at BAR5=0x%x, ports=%d", ahci_bar5, ports);

    ahci_found = 1;

    struct devfs_device ddev;
    klibc.snprintf(ddev.name, sizeof(ddev.name), "ahci");
    ddev.type = DEVFS_BLOCK_DEV;
    ddev.block_size = 512;
    ddev.block_count = 0;
    klibc.snprintf(ddev.model, sizeof(ddev.model), "AHCI controller, %d ports", ports);
    ddev.priv = 0;
    ddev.read = 0;
    ddev.write = 0;
    devfs_register(&ddev);

    klog("ahci", "ahci_init() done");
    return 0;
}
