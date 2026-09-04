VER=XKERN_26.0.8
TYPE=TEST
OS=XOS
Q = @

BX = aptic/buildx/buildx
CC = gcc
STRIP = strip
AS = nasm
LD = ld
TARGET = build/xkern
CFLAGS_COMMON = -m64 -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -Wno-error \
         -Ilibkern/libkern -Iosfmk/kern -Iosfmk/x86_64 -Iosfmk/vm -Iosfmk/console \
         -Ipexpert/x86_64          -Ibsd/kern/init_ram_getty -Ibsd/kern/elf -Ibsd/devfs \
         -Ibsd/fs/ramfs -Ibsd/fs/ramfs/include \
         -Ibsd/crypto \
         -Iiokit -Iiokit/IOStorageFamily -Iiokit/IOGraphicsFamily -Iiokit/IOHIDFamily \
         -Iiokit/IONetFamily \
         -Iosfmk/dma \
         -Iinclude -Ibsd -Igame -Ipexpert\
          -Idevkits/gpukit/include -Idevkits/gpukit/include/gpukit -Idevkits/gpukit/xenv -Idevkits/gpukit/recovery -Idevkits/gpukit/installer -Idevkits/gpukit/desktop -Idevkits/gpukit/BootScreen -Idevkits/gpukit/lvgl \
          -mno-red-zone -MMD -MP \
          -Iiokit/IOUSBFamily/core -Iiokit/IOUSBFamily/hid \
           -Idevkits -Idevkits/xkos_gui/widget -Idevkits/xkos_gui/dbus -Idevkits/xkos_gui/de \
           -I. -Iapi -Ilibkern/libcpp \
           -Wno-unused-parameter \
           -Wno-unused-function\
           -Wno-unused-variable\
           -Wno-sign-compare\
           -Wno-format \
           -Wno-int-to-pointer-cast \
           -Wno-unused-but-set-variable \
           -Wno-unterminated-string-initialization \
           -Wno-cast-function-type \
           -Wno-pointer-to-int-cast

CFLAGS   = $(CFLAGS_COMMON) -std=gnu11 \
           -Wno-implicit-function-declaration -Wno-implicit-int -Wno-conflicting-types \
           -Wno-incompatible-pointer-types -Wno-int-conversion
CXX     = g++
# C++ kernel features: freestanding, no RTTI/exceptions (no libstdc++ runtime
# in the kernel).
CXXFLAGS = $(CFLAGS_COMMON) -std=c++20 -fno-rtti -fno-exceptions -Wno-unterminated-string-initialization -Wno-pointer-to-int-cast -Wno-comment \
            -Wno-volatile -Wno-conflicting-types


DEPFILES = $(OBJS:.o=.d)
ASFLAGS = -f elf64
LDFLAGS = -m elf_x86_64 -T linker.ld

APTICC = tools/xkutil/bin/apticc
APVM_LD = tools/xkutil/bin/apvm-ld

BOOT_OBJS = pexpert/x86_64/boot.o pexpert/x86_64/loader_config.o \
            pexpert/x86_64/multiboot2.o
CPU_OBJS  = osfmk/x86_64/cpu.o osfmk/x86_64/clock.o pexpert/x86_64/smbios.o osfmk/fpu/fpu.o osfmk/fpu/main.o
MEM_OBJS  = pexpert/x86_64/e820.o osfmk/vm/pmm.o osfmk/vm/paging.o
DRV_OBJS  = osfmk/console/vga.o osfmk/console/serial.o libkern/libkern/klog.o libkern/libkern/logger.o \
            iokit/IOHIDFamily/atkbd.o iokit/IOHIDFamily/atmouse.o \
            osfmk/x86_64/idt.o osfmk/x86_64/pic.o osfmk/x86_64/isr.o
KERN_OBJS = osfmk/kern/main.o osfmk/kern/version.o osfmk/kern/sh.o osfmk/kern/dinit.o osfmk/kern/panic.o \
            iokit/IOGraphicsFamily/fb.o iokit/IOGraphicsFamily/font9x8.o iokit/IOGraphicsFamily/font6x12.o \
            iokit/IOGraphicsFamily/font.o iokit/IOGraphicsFamily/buffer.o iokit/IOServiceFamily/io_service.o
GAME_OBJS = game/gfx.o game/input.o game/engine.o game/demo.o game/pizza.o game/doom.o pexpert/hw-report.o pexpert/x86_64/cpu.o
include devkits/gpukit/lvgl/Makefile
LVGL_OBJ := $(LVGL_SRC:.c=.o)

GPUKIT_OBJS = devkits/gpukit/src/lv_port.o devkits/gpukit/src/lv_console.o \
              devkits/gpukit/BootScreen/boot.o devkits/gpukit/BootScreen/plymouth.o \
              devkits/gpukit/BootScreen/background_tasks.o
# macOS-styled session layers: environment chrome, recovery menu, installer
# wizard, desktop environment, and the embedded install image payload.
XENV_OBJS = devkits/gpukit/xenv/xenv.o \
            devkits/gpukit/recovery/xrecovery.o \
            devkits/gpukit/installer/xinstall.o \
             devkits/gpukit/desktop/xdesktop.o \
             devkits/gpukit/payload/install_payload.o
# XKOS GUI kit: D-Bus bus, Cupertino widgets, main-env chrome and the DE.
XKOS_GUI_OBJS = \
             devkits/xkos_gui/dbus/dbus.o \
             devkits/xkos_gui/widget/xkos_ui.o \
             devkits/xkos_gui/widget/clock.o \
             devkits/xkos_gui/widget/dialouge.o \
             devkits/xkos_gui/widget/notification.o \
             devkits/xkos_gui/widget/warning.o \
             devkits/xkos_gui/widget/menu.o \
             devkits/xkos_gui/widget/toggle.o \
             devkits/xkos_gui/widget/slider.o \
             devkits/xkos_gui/widget/popover.o \
             devkits/xkos_gui/widget/segmented.o \
             devkits/xkos_gui/widget/search.o \
             devkits/xkos_gui/main_env/top.o \
             devkits/xkos_gui/main_env/dock.o \
             devkits/xkos_gui/main_env/app_body.o \
             devkits/xkos_gui/de/xkos_de.o
KLIB_OBJS = libkern/libkern/stdint.o libkern/libkern/stdarg.o libkern/libkern/printf.o \
            libkern/libkern/scanf.o libkern/libkern/string.o libkern/libkern/stdlib.o \
            libkern/libkern/ctype.o \
            libkern/libkern/klibc.o \
            libkern/libkern/udivdi3.o \
            libkern/libkern/float.o \
            libkern/libkern/math.o \

APTIC_OBJS = $(patsubst %.at,%.o,$(wildcard xom/*.at))

#kernel CXX objects
KERN_CXXO = osfmk/kern/sub_init.o

#check for headers

all: header-check $(TARGET)

HEADER_CHECK := scripts/header_check.sh
HEADER_STAMP := .header_check_done
header-check: $(HEADER_STAMP)

$(HEADER_STAMP): $(HEADER_CHECK)
	$(Q)echo "CHECK   headers"
	$(Q)$(HEADER_CHECK)
	$(Q)touch $@

%.o: %.at $(APTICC) $(APVM_LD)
	$(Q)echo "APTICC  $<"
	$(Q)$(APTICC) $< $*.oarc
	$(Q)$(APVM_LD) $*.oarc $@
	$(Q)rm -f $*.oarc

include iokit/IOStorageFamily/Makefile
include iokit/IOStorageFamily/ata/Makefile
include iokit/IOStorageFamily/sata/Makefile
include iokit/IOStorageFamily/ahci/Makefile
include iokit/IOStorageFamily/nvme/Makefile
include iokit/IOStorageFamily/ssd/Makefile
include iokit/IOStorageFamily/devfs/Makefile
include iokit/IOPCIFamily/Makefile
include iokit/IONetFamily/Makefile
include iokit/IOAudioFamily/Makefile
SMP_OBJS  = osfmk/x86_64/smp.o osfmk/x86_64/lapic.o osfmk/x86_64/ioapic.o osfmk/x86_64/spinlock.o
IOTIME_OBJS = osfmk/x86_64/pit.o osfmk/x86_64/tsc.o
include iokit/IOUSBFamily/core/Makefile
include iokit/IOUSBFamily/usb1/Makefile
include iokit/IOUSBFamily/usb2/Makefile
include iokit/IOUSBFamily/usb3/Makefile
include iokit/IOUSBFamily/hub/Makefile
include iokit/IOUSBFamily/hid/Makefile
include iokit/IOUSBFamily/dev/Makefile
include iokit/IOUSBFamily/msc/Makefile
include bsd/kern/init_ram_getty/Makefile
include bsd/kern/elf/Makefile
include bsd/devfs/Makefile
include bsd/ext3/Makefile
include bsd/fs/ramfs/Makefile
include bsd/crypto/Makefile
include osfmk/x86_64/acpi/Makefile
include osfmk/dma/Makefile

# Add your C++ kernel feature objects here, e.g. CPP_OBJS = myfeature/foo.o
CPP_OBJS = libkern/libcpp/demo.o libkern/libcpp/runtime.o iokit/IOPCIFamily/generic_floppy.o

OBJS = $(BOOT_OBJS) $(CPU_OBJS) $(MEM_OBJS) $(DRV_OBJS) $(KERN_OBJS) $(KLIB_OBJS) \
       $(GAME_OBJS) $(LVGL_OBJ) $(GPUKIT_OBJS) $(XENV_OBJS) $(XKOS_GUI_OBJS) \
        $(IO_MAIN_OBJ) $(ATA_OBJS) $(SATA_OBJS) $(AHCI_OBJS) $(NVME_OBJS) $(SSD_OBJS) \
       $(DEVFS_OBJS) $(IOPCI_OBJS) $(IONET_OBJS) $(HDA_OBJS)        $(IOTIME_OBJS) $(INITRAM_OBJS) $(ELF_OBJS) $(SMP_OBJS) \
       $(BSD_DEVFS_OBJS) \
        $(EXT3_OBJS) \
        $(RAMFS_OBJS) \
        $(CRYPTO_OBJS) \
       $(ACPI_OBJS) \
       $(DMA_OBJS) \
       $(USB_CORE_OBJS) $(USB1_OBJS) $(USB2_OBJS) $(USB3_OBJS) $(USBHUB_OBJS) $(USBHID_OBJS) \
       $(USBDEV_OBJS) $(USBMSC_OBJS) \
        $(APTIC_OBJS) $(CPP_OBJS) $(KERN_CXXO)

-include $(DEPFILES)

osfmk/kern/version.c: scripts/version_maker.sh
	$(Q)echo "GEN     $@"
	$(Q)./scripts/version_maker.sh


osfmk/kern/version.o: osfmk/kern/version.c

osfmk/x86_64/clock.o: osfmk/x86_64/clock.asm
	$(Q)echo "AS      $<"
	$(Q)$(AS) $(ASFLAGS) $< -o $@


pexpert/x86_64/boot.o: pexpert/x86_64/boot.asm
	$(Q)echo "AS      $<"
	$(Q)$(AS) $(ASFLAGS) $< -o $@

osfmk/x86_64/isr.o: osfmk/x86_64/isr.asm
	$(Q)echo "AS      $<"
	$(Q)$(AS) $(ASFLAGS) $< -o $@

osfmk/x86_64/trampoline.bin: osfmk/x86_64/trampoline.S
	$(Q)echo "NASM    $<"
	$(Q)$(AS) -f bin $< -o $@

osfmk/x86_64/smp.o: osfmk/x86_64/smp.c osfmk/x86_64/trampoline.bin
	$(Q)echo "CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

osfmk/fpu/main.o: osfmk/fpu/main.S
	$(Q)echo "AS      $<"
	$(Q)$(AS) $(ASFLAGS) $< -o $@

%.o: %.c
	$(Q)echo "CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# C++ kernel features. Link the resulting .o into the kernel by adding it to
# CPP_OBJS below (or to OBJS directly).
%.o: %.cpp
	$(Q)echo "CXX     $<"
	$(Q)$(CXX) $(CXXFLAGS) -c $< -o $@

# The raycaster does its heavy lifting (per-pixel floor/wall casting) in C,
# so it gets -O2 while the rest of the kernel stays at default -O0.
# It also uses float math whose ABI returns live in xmm registers, so this
# one file opts back into SSE (CR4.OSFXSR is enabled by the boot trampoline;
# no interrupt/exception path uses floating point).
game/doom.o: game/doom.c
	$(Q)echo "CC      $< (O2)"
	$(Q)$(CC) $(CFLAGS) -O2 -msse -msse2 -c $< -o $@

$(TARGET): $(OBJS) linker.ld
	$(Q)echo "LD      $@"
	$(Q)$(LD) $(LDFLAGS) $(OBJS) -o $(TARGET)

# Embedded install image payload.  install_payload.S is regenerated from
# install.img (see scripts/gen_payload.sh); the two-pass build in
# scripts/build_full_iso.sh produces the final bootable ISO.
INITRAMFS  = iso/boot/init.cpio
PAYLOAD_S  = devkits/gpukit/payload/install_payload.S

$(PAYLOAD_S):
	$(Q)echo "PAYLOAD gen $@"
	$(Q)scripts/gen_payload.sh

# install_payload.o <- install_payload.S (GAS .incbin); rebuilt when the
# install image changes so the final kernel embeds the current payload.
# (wildcard: no hard dependency while install.img does not exist yet)
devkits/gpukit/payload/install_payload.o: $(PAYLOAD_S) $(wildcard install.img)
	$(Q)echo "CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

payload: $(PAYLOAD_S)
	$(Q)echo "PAYLOAD refreshed (next 'make' embeds install.img in the kernel)"

$(INITRAMFS):
	$(Q)echo "INITRAM build $@"
	$(Q)scripts/build_initramfs.sh

run-initrd: $(TARGET).iso
	$(Q)echo "QEMU    $< (GRUB BIOS boot, initramfs module)"
	$(Q)qemu-system-x86_64 -cdrom $(TARGET).iso -serial stdio

usb-run: $(TARGET)
	$(Q)echo "QEMU    $< (OHCI + USB keyboard; click window to type, or type here in the terminal)"
	$(Q)qemu-system-x86_64 -cpu qemu64 -cdrom xkern.iso -device pci-ohci -device usb-kbd -nographic

usb-run-xhci: $(TARGET).iso
	$(Q)echo "QEMU    $< (xHCI + USB keyboard; click window to type, or type here in the terminal)"
	$(Q)qemu-system-x86_64 -cpu qemu64 -cdrom xkern.iso -serial stdio -device qemu-xhci -device usb-kbd

usb-run-all: $(TARGET).iso
	$(Q)echo "QEMU    $< (xHCI + keyboard + mouse + mass storage)"
	$(Q)qemu-img create -f raw /tmp/usbtest.img 64M
	$(Q)qemu-system-x86_64 -cpu qemu64 -cdrom build/xkern.iso -serial stdio -device qemu-xhci -vga cirrus\
		-device usb-kbd -device usb-mouse \
		-drive if=none,id=d0,file=/tmp/usbtest.img,format=raw \
		-device usb-storage,drive=d0

usb-run-ehci: $(TARGET).iso
	$(Q)echo "QEMU    $< (EHCI + keyboard + mouse; watch serial for EHCI klog)"
	$(Q)qemu-system-x86_64 -cpu qemu64 -cdrom $(TARGET).iso -serial stdio \
		-device usb-ehci,id=ehci -device usb-kbd -device usb-mouse

GRUB_PLAT_DIR := $(shell find /usr/lib/grub -maxdepth 1 -type d -name 'i386-pc' 2>/dev/null | head -1)
ifeq ($(GRUB_PLAT_DIR),)
GRUB_PLAT_DIR := tools/grub/i386-pc
endif

GRUB_PC_VER = 2.12-1ubuntu7.3
GRUB_PC_DEB = tools/grub/grub-pc-bin_$(GRUB_PC_VER)_amd64.deb
GRUB_PC_URL = "https://archive.ubuntu.com/ubuntu/pool/main/g/grub2/grub-pc-bin_$(GRUB_PC_VER)_amd64.deb"

ifeq ($(GRUB_PLAT_DIR),tools/grub/i386-pc)
$(GRUB_PLAT_DIR)/boot.img:
	$(Q)echo "GRUB    build grub-pc platform modules (tools/grub/i386-pc)"
	$(Q)mkdir -p tools/grub
	$(Q)if [ ! -f $(GRUB_PC_DEB) ]; then \
		echo "GRUB    download grub-pc-bin $(GRUB_PC_VER)"; \
		curl -fsSL -o $(GRUB_PC_DEB) $(GRUB_PC_URL); \
	fi
	$(Q)rm -rf tools/grub/deb tools/grub/i386-pc
	$(Q)mkdir -p tools/grub/deb
	$(Q)dpkg-deb -x $(GRUB_PC_DEB) tools/grub/deb
	$(Q)cp -r tools/grub/deb/usr/lib/grub/i386-pc tools/grub/i386-pc
	$(Q)rm -rf tools/grub/deb
endif

$(TARGET).iso: $(TARGET) $(INITRAMFS) $(GRUB_PLAT_DIR)/boot.img
	$(Q)echo "ISO     $@ (grub-mkrescue, BIOS, initramfs module)"
	$(Q)mkdir -p iso/boot/grub iso/boot/grub/fonts
	$(Q)cp $(TARGET) iso/boot/kernel
	$(Q)cp /usr/share/grub/unicode.pf2 iso/boot/grub/fonts/unicode.pf2
	$(Q)grub-mkrescue --directory=$(GRUB_PLAT_DIR) -o $@ iso

iso: $(TARGET).iso

install-headers:
	$(Q)echo "INSTALL headers -> /usr/include/xkern"
	$(Q)mkdir -p /usr/include/xkern
	$(Q)find include libkern/libkern libkern/libcpp \
		osfmk/kern osfmk/x86_64 osfmk/vm osfmk/console osfmk/fpu osfmk/dma \
		osfmk/x86_64/acpi \
		pexpert pexpert/x86_64 \
		iokit iokit/IOStorageFamily iokit/IOStorageFamily/ata iokit/IOStorageFamily/sata \
		iokit/IOStorageFamily/ahci iokit/IOStorageFamily/nvme iokit/IOStorageFamily/ssd \
		iokit/IOStorageFamily/devfs \
		iokit/IOPCIFamily iokit/IONetFamily iokit/IOAudioFamily \
		iokit/IOGraphicsFamily iokit/IOHIDFamily iokit/IOServiceFamily \
		iokit/IOUSBFamily/core iokit/IOUSBFamily/usb1 iokit/IOUSBFamily/usb2 \
		iokit/IOUSBFamily/usb3 iokit/IOUSBFamily/hub iokit/IOUSBFamily/hid \
		iokit/IOUSBFamily/dev iokit/IOUSBFamily/msc \
		bsd/kern/init_ram_getty bsd/kern/elf bsd/devfs bsd/fs/ramfs/include bsd/crypto \
		game api devkits \
		-type f -name '*.h' -print0 | while IFS= read -r -d '' f; do \
			dir=$$(dirname "$$f"); \
			mkdir -p "/usr/include/xkern/$$dir"; \
			install -m 0644 "$$f" "/usr/include/xkern/$$f"; \
		done
	$(Q)echo "DONE   headers installed to /usr/include/xkern"

clean:
	$(Q)echo "CLEAN"
	$(Q)rm -f $(OBJS) $(DEPFILES) $(TARGET) $(INITRAMFS) $(PAYLOAD_S)
	$(Q)rm -f osfmk/x86_64/trampoline.bin
	$(Q)rm -rf initrd_stage xkern.iso
	$(Q)rm osfmk/kern/version.c

run: $(TARGET).iso
	$(Q)echo "QEMU    $< (GRUB BIOS boot, 4 CPUs)"
	$(Q)qemu-system-x86_64 -smp 4 -cdrom $(TARGET).iso -serial stdio

qemu: $(TARGET).iso
	$(Q)echo "QEMU    $<"
	$(Q)qemu-system-x86_64 -cdrom $(TARGET).iso


help:
	@echo "all - does all"
	@echo "clean - cleans build files"
	@echo "run - runs the kernel with 4 threads"
	@echo "iso - generates iso"
	@echo "gen_ver - generates version.c"
	@echo "install-headers - installs header files to /usr/include/xkern/"

.PHONY: all clean run qemu run-initrd usb-run usb-run-xhci usb-run-all xkutil iso payload gen_ver install-headers help
