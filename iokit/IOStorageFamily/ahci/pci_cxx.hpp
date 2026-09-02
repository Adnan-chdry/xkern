#pragma once
/*
 * pci_cxx.hpp - C++-safe view of the functions this driver needs from
 * IOPCIFamily/pci.h.  The real header cannot be included from C++ because it
 * names a parameter `class` (a reserved word in C++); the underlying symbols
 * are plain C, so we re-declare the few we use with harmless parameter names.
 */
#include "types.h"

#define PCI_CLASS_MASS_STORAGE  0x01
#define PCI_SUBCLASS_SATA       0x06
#define PCI_BAR5                0x24
#define PCI_COMMAND             0x04

extern "C" {
    u32  pci_config_read(u8 bus, u8 dev, u8 func, u8 offset);
    void pci_config_write(u8 bus, u8 dev, u8 func, u8 offset, u32 val);
    int  pci_find_class(u8 cls, u8 sub, u8 progif,u8 *bus, u8 *dev, u8 *func);
}
