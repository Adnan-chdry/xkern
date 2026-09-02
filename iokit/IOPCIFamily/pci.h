#ifndef IOPCI_H
#define IOPCI_H

#include "types.h"

#define PCI_CONFIG_ADDR      0xCF8
#define PCI_CONFIG_DATA      0xCFC

#define PCI_MAX_BUSES        256
#define PCI_MAX_DEVICES      32
#define PCI_MAX_FUNCTIONS    8
#define PCI_MAX_DETECTED     512

#define PCI_VENDOR_ID        0x00
#define PCI_DEVICE_ID        0x02
#define PCI_COMMAND          0x04
#define PCI_STATUS           0x06
#define PCI_REVISION_ID      0x08
#define PCI_PROGIF           0x09
#define PCI_SUBCLASS         0x0A
#define PCI_CLASS            0x0B
#define PCI_CACHE_LINE       0x0C
#define PCI_LATENCY_TIMER    0x0D
#define PCI_HEADER_TYPE      0x0E
#define PCI_BIST             0x0F
#define PCI_BAR0             0x10
#define PCI_BAR1             0x14
#define PCI_BAR2             0x18
#define PCI_BAR3             0x1C
#define PCI_BAR4             0x20
#define PCI_BAR5             0x24
#define PCI_CARDBUS_CIS      0x28
#define PCI_SUBSYSTEM_VENDOR 0x2C
#define PCI_SUBSYSTEM_ID     0x2E
#define PCI_EXPANSION_ROM    0x30
#define PCI_CAPABILITIES     0x34
#define PCI_INTERRUPT_LINE   0x3C
#define PCI_INTERRUPT_PIN    0x3D
#define PCI_MIN_GNT           0x3E
#define PCI_MAX_LAT           0x3F

#define PCI_CLASS_MASS_STORAGE      0x01
#define PCI_SUBCLASS_SCSI           0x00
#define PCI_SUBCLASS_IDE            0x01
#define PCI_SUBCLASS_SATA           0x06
#define PCI_SUBCLASS_NVME           0x08
#define PCI_CLASS_DISPLAY           0x03
#define PCI_CLASS_BRIDGE            0x06
#define PCI_SUBCLASS_HOST_BRIDGE    0x00
#define PCI_SUBCLASS_PCI_BRIDGE     0x04
#define PCI_CLASS_SERIAL            0x0C
#define PCI_SUBCLASS_USB            0x03
#define PCI_PROGIF_UHCI             0x00
#define PCI_PROGIF_OHCI             0x10
#define PCI_PROGIF_EHCI             0x20
#define PCI_PROGIF_XHCI             0x30

#define PCI_VENDOR_INTEL            0x8086

/* secondary bus number of a PCI-to-PCI bridge */
#define PCI_SECONDARY_BUS           0x19

/* Intel PCH USB port routing (Panther/Lynx/Wildcat Point) */
#define PCI_INTEL_XUSB2PR           0xD0  /* route USB2 ports to xHCI   */
#define PCI_INTEL_PSSEN             0xD8  /* EHCI speed sense enable    */

struct pci_device {
    u8  bus;
    u8  dev;
    u8  func;
    u16 vendor_id;
    u16 device_id;
    u8  class_code;
    u8  subclass;
    u8  progif;
    u8  header_type;
    u32 bar[6];
    u8  irq_line;
    int present;
};

u32 pci_config_read(u8 bus, u8 dev, u8 func, u8 offset);
u8   pci_config_read_byte(u8 bus, u8 dev, u8 func, u8 offset);
void pci_config_write(u8 bus, u8 dev, u8 func, u8 offset, u32 val);
int  pci_scan(void);
/*
 * Find a device by class.  progif = 0xFF acts as a wildcard
 * (matches any programming interface).
 */
int  pci_find_class(u8 clas_code, u8 subclass, u8 progif,
                    u8 *bus, u8 *dev, u8 *func);
struct pci_device *pci_find_vendev(u16 vendor, u16 device);
void pci_list(void);
struct pci_device *pci_get_device(int index);
int  pci_device_count(void);

#endif
