#!/usr/bin/env bash

#
# Xkern header checker
#

ROOT="${1:-.}"

REQUIRED_HEADERS=(
    "/usr/include/openssl/*"

    "api/ctype.hpp"
    "api/klibc.hpp"
    "api/klog.hpp"
    "api/libkern_api.hpp"
    "api/logger.hpp"
    "api/printf.hpp"
    "api/scanf.hpp"
    "api/stdarg.hpp"
    "api/stdint.hpp"
    "api/stdio.hpp"
    "api/stdlib.hpp"
    "api/string.hpp"
    "api/udivdi3.hpp"

    "include/ctype.h"
    "include/idt.h"
    "include/io.h"
    "include/kernel.h"
    "include/limits.h"
    "include/logger.h"
    "include/multiboot.h"
    "include/paging.h"
    "include/pic.h"
    "include/pmm.h"
    "include/printf.h"
    "include/scanf.h"
    "include/serial.h"
    "include/smbios.h"
    "include/stdarg.h"
    "include/stdio.h"
    "include/stdlib.h"
    "include/string.h"
    "include/syscall.h"
    "include/types.h"
    "include/vga.h"

    "iokit/IOAudioFamily/hda.h"

    "iokit/IOGraphicsFamily/buffer.h"
    "iokit/IOGraphicsFamily/fb.h"
    "iokit/IOGraphicsFamily/font.h"

    "iokit/IOHIDFamily/atkbd.h"
    "iokit/IOHIDFamily/atmouse.h"

    "iokit/IONetFamily/arp.h"
    "iokit/IONetFamily/dhcp.h"
    "iokit/IONetFamily/e1000.h"
    "iokit/IONetFamily/ether.h"
    "iokit/IONetFamily/icmp.h"
    "iokit/IONetFamily/ionet.h"
    "iokit/IONetFamily/ipv4.h"
    "iokit/IONetFamily/netdev.h"
    "iokit/IONetFamily/udp.h"

    "iokit/IOPCIFamily/floppy_api.h"
    "iokit/IOPCIFamily/generic_floppy.hpp"
    "iokit/IOPCIFamily/pci.h"

    "iokit/IOServiceFamily/io_service.h"

    "iokit/IOStorageFamily/ata.h"
    "iokit/IOStorageFamily/io_storage.h"
    "iokit/IOStorageFamily/ahci/ahci.hpp"
    "iokit/IOStorageFamily/ahci/dynamic_util.hpp"
    "iokit/IOStorageFamily/ahci/pci_cxx.hpp"
    "iokit/IOStorageFamily/devfs/devfs.h"
    "iokit/IOStorageFamily/nvme/nvme.h"
    "iokit/IOStorageFamily/sata/ahci.h"
    "iokit/IOStorageFamily/ssd/ssd.h"

    "iokit/IOUSBFamily/core/usb.h"
    "iokit/IOUSBFamily/core/usb_dma.h"
    "iokit/IOUSBFamily/core/usb_irq.h"
    "iokit/IOUSBFamily/dev/usbdev.h"
    "iokit/IOUSBFamily/hid/usbhid.h"
    "iokit/IOUSBFamily/hub/usbhub.h"
    "iokit/IOUSBFamily/msc/usbmsc.h"
    "iokit/IOUSBFamily/usb1/ohci.h"
    "iokit/IOUSBFamily/usb1/uhci.h"
    "iokit/IOUSBFamily/usb2/ehci.h"
    "iokit/IOUSBFamily/usb3/xhci.h"

    "libkern/libcpp/libcpp.h"
    "libkern/libcpp/libcpp.hpp"
    "libkern/libcpp/libpci.hpp"
    "libkern/libkern/klibc.h"
    "libkern/libkern/klog.h"

    "osfmk/kern/dinit.h"
    "osfmk/kern/panic.h"
    "osfmk/x86_64/cpu.h"
    "osfmk/x86_64/pit.h"
    "osfmk/x86_64/smp.h"
    "osfmk/x86_64/spinlock.h"
    "osfmk/x86_64/tsc.h"

    "pexpert/hw-report.h"
    "pexpert/x86_64/e820.h"
    "pexpert/x86_64/loader_config.h"
    "pexpert/x86_64/multiboot2.h"

    "bsd/devfs/devfs.h"
    "bsd/kern/elf/elf.h"
    "bsd/kern/init_ram_getty/init_ram_getty.h"
    "bsd/fs/ramfs/include/ramfs.hpp"
    "bsd/fs/ramfs/include/ramfs_compat.h"

    "game/demo.h"
    "game/doom.h"
    "game/engine.h"
    "game/game.h"
    "game/gfx.h"
    "game/input.h"
    "game/pizza.h"
)

failed=0

for header in "${REQUIRED_HEADERS[@]}"; do
    printf "checking for %s... " "$header"

    # Absolute path: use it directly.
    # Relative path: resolve it against ROOT.
    if [[ "$header" == /* ]]; then
        path="$header"
    else
        path="$ROOT/$header"
    fi

    # Handle glob patterns such as /usr/include/openssl/*
    if [[ "$path" == *'*'* || "$path" == *'?'* || "$path" == *'['* ]]; then
        if compgen -G "$path" > /dev/null 2>&1; then
            printf "found\n"
        else
            printf "missing\n"
            failed=1
        fi

    # Normal file
    elif [[ -f "$path" ]]; then
        printf "found\n"

    else
        printf "missing\n"
        failed=1
    fi
done

exit "$failed"
