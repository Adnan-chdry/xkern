xkern-26.0.8
============

An x86_64 kernel. MIT licensed.

What it is:
  Multiboot2-booted kernel with SMP, framebuffer, LVGL console, PCI,
  USB (xHCI/EHCI/OHCI), storage (ATA/SATA/AHCI/NVMe), crypto (SHA-256,
  AES-256), network stack, ELF loader, initramfs support.

What's next:
  - Block device logging for panic dumps
  - Local networking support
  - Auto-generated boot config
  - Hardware report scan re-enablement

Contributing:
  Fork, branch, commit, PR.
  Keep it C11, freestanding, -ffreestanding -fno-pie.
  Run 'make' before pushing.
  No tabs in new code if existing neighbours use spaces.

Build:
  mkdir build && make

Run:
  make run
