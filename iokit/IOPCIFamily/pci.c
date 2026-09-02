#include "pci.h"
#include "io.h"
#include "stdio.h"
#include "klog.h"
#include "klibc.h"
#include <stddef.h>

static struct pci_device g_pci_devices[PCI_MAX_DETECTED];
static int g_pci_count;
static u8 g_scanned_buses[PCI_MAX_BUSES / 8];

static void pci_scan_bus(u8 bus);

u32 pci_config_read(u8 bus, u8 dev, u8 func, u8 offset) {
    u32 addr = (u32)((bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

/* read a single byte lane from config space */
u8 pci_config_read_byte(u8 bus, u8 dev, u8 func, u8 offset) {
    return (pci_config_read(bus, dev, func, offset)
            >> ((offset & 3) * 8)) & 0xFF;
}

void pci_config_write(u8 bus, u8 dev, u8 func, u8 offset, u32 val) {
    u32 addr = (u32)((bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

static void pci_add_device(u8 bus, u8 dev, u8 func) {
    if (g_pci_count >= PCI_MAX_DETECTED)
        return;

    u32 id = pci_config_read(bus, dev, func, PCI_VENDOR_ID);
    u16 vendor = id & 0xFFFF;
    u16 device = (id >> 16) & 0xFFFF;

    u32 class_rev = pci_config_read(bus, dev, func, PCI_REVISION_ID);
    u8 class_code = (class_rev >> 24) & 0xFF;
    u8 subclass   = (class_rev >> 16) & 0xFF;
    u8 progif     = (class_rev >> 8) & 0xFF;

    u32 hdr = pci_config_read(bus, dev, func, PCI_HEADER_TYPE);
    u8 header_type = hdr & 0xFF;

    struct pci_device *pd = &g_pci_devices[g_pci_count];
    pd->bus = bus;
    pd->dev = dev;
    pd->func = func;
    pd->vendor_id = vendor;
    pd->device_id = device;
    pd->class_code = class_code;
    pd->subclass = subclass;
    pd->progif = progif;
    pd->header_type = header_type;

    if ((header_type & 0x7F) == 0x00) {
        for (int i = 0; i < 6; i++)
            pd->bar[i] = pci_config_read(bus, dev, func, PCI_BAR0 + i * 4);

        u32 irq = pci_config_read(bus, dev, func, PCI_INTERRUPT_LINE);
        pd->irq_line = irq & 0xFF;
    } else {
        for (int i = 0; i < 6; i++)
            pd->bar[i] = 0;
        pd->irq_line = 0;
    }

    pd->present = 1;
    g_pci_count++;
}

static void pci_check_function(u8 bus, u8 dev, u8 func) {
    u32 class_rev = pci_config_read(bus, dev, func, PCI_REVISION_ID);
    u8 class_code = (class_rev >> 24) & 0xFF;
    u8 subclass   = (class_rev >> 16) & 0xFF;

    pci_add_device(bus, dev, func);

    /* recurse through PCI-to-PCI bridges */
    if (class_code == PCI_CLASS_BRIDGE && subclass == PCI_SUBCLASS_PCI_BRIDGE) {
        u8 hdr = pci_config_read_byte(bus, dev, func, PCI_HEADER_TYPE);
        if ((hdr & 0x7F) == 0x01) {
            u8 secondary = pci_config_read_byte(bus, dev, func,
                                            PCI_SECONDARY_BUS);
            if (secondary)
                pci_scan_bus(secondary);
        }
    }
}

static void pci_check_device(u8 bus, u8 dev) {
    u32 id = pci_config_read(bus, dev, 0, PCI_VENDOR_ID);
    if (id == 0xFFFFFFFF)
        return;

    u32 hdr = pci_config_read(bus, dev, 0, PCI_HEADER_TYPE);
    int multifunc = (hdr & 0x80) != 0;

    /* enumerate each function exactly once */
    for (u8 func = 0; func < PCI_MAX_FUNCTIONS; func++) {
        id = pci_config_read(bus, dev, func, PCI_VENDOR_ID);
        if (id == 0xFFFFFFFF)
            continue;
        pci_check_function(bus, dev, func);
        if (!multifunc)
            break;
    }
}

static void pci_scan_bus(u8 bus) {
    if (g_scanned_buses[bus >> 3] & (1 << (bus & 7)))
        return;
    g_scanned_buses[bus >> 3] |= 1 << (bus & 7);

    for (u8 dev = 0; dev < PCI_MAX_DEVICES; dev++)
        pci_check_device(bus, dev);
}

int pci_scan(void) {
    g_pci_count = 0;
    klibc.memset(g_scanned_buses, 0, sizeof(g_scanned_buses));

    u32 hdr = pci_config_read(0, 0, 0, PCI_HEADER_TYPE);
    if ((hdr & 0x80) == 0) {
        pci_scan_bus(0);
    } else {
        for (int bus = 0; bus < PCI_MAX_BUSES; bus++)
            pci_scan_bus(bus);
    }

    klog("PCI", "%d devices found", g_pci_count);
    return g_pci_count;
}

int pci_find_class(u8 class, u8 subclass, u8 progif,
                   u8 *bus, u8 *dev, u8 *func) {
    for (int i = 0; i < g_pci_count; i++) {
        struct pci_device *pd = &g_pci_devices[i];
        if (pd->class_code == class && pd->subclass == subclass
            && (progif == 0xFF || pd->progif == progif)) {
            *bus = pd->bus;
            *dev = pd->dev;
            *func = pd->func;
            return 0;
        }
    }
    return -1;
}

struct pci_device *pci_find_vendev(u16 vendor, u16 device) {
    for (int i = 0; i < g_pci_count; i++) {
        struct pci_device *pd = &g_pci_devices[i];
        if (pd->vendor_id == vendor && pd->device_id == device)
            return pd;
    }
    return NULL;
}

static const char *pci_class_name(u8 class, u8 subclass) {
    if (class == 0x00) return "Unclassified";
    if (class == 0x01) {
        if (subclass == 0x00) return "SCSI";
        if (subclass == 0x01) return "IDE";
        if (subclass == 0x06) return "SATA";
        if (subclass == 0x08) return "NVMe";
        return "Mass Storage";
    }
    if (class == 0x02) return "Network";
    if (class == 0x03) return "Display";
    if (class == 0x04) return "Multimedia";
    if (class == 0x06) {
        if (subclass == 0x00) return "Host Bridge";
        if (subclass == 0x04) return "PCI Bridge";
        return "Bridge";
    }
    if (class == 0x08) return "System";
    if (class == 0x0C) return "Serial Bus";
    return "Other";
}

void pci_list(void) {
    for (int i = 0; i < g_pci_count; i++) {
        struct pci_device *pd = &g_pci_devices[i];
        klog("PCI", "%d:%d:%d vendor 0x%x device 0x%x %s",
             pd->bus, pd->dev, pd->func,
             pd->vendor_id, pd->device_id,
             pci_class_name(pd->class_code, pd->subclass));
    }
}

struct pci_device *pci_get_device(int index) {
    if (index < 0 || index >= g_pci_count)
        return 0;
    return &g_pci_devices[index];
}

int pci_device_count(void) {
    return g_pci_count;
}
