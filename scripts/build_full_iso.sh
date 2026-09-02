#!/bin/sh
#
# build_full_iso.sh - two-pass build of the full XKERN bootable ISO.
#
# Pass 1: build a plain kernel (empty install payload) + the recovery
#         initramfs, then grub-mkrescue them into install.img.  install.img
#         is isohybrid (its first bytes are a valid boot sector), so the
#         installer can byte-copy it to a disk with devfs_write and the
#         result boots on its own.
# Pass 2: regenerate the payload stub so install.img is embedded in the
#         kernel's .install_payload section, rebuild the kernel, then
#         grub-mkrescue the final xkern.iso.
#
# Usage: scripts/build_full_iso.sh [kernel-image-name]
#   kernel-image-name   default xkern

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL="${1:-xkern}"
GRUB_DIR="$(grep -m1 '^GRUB_PLAT_DIR *=' "$ROOT/Makefile" | cut -d= -f2 | xargs)"

cd "$ROOT"

echo "== pass 1: plain kernel + initramfs -> install.img"
rm -f install.img
scripts/gen_payload.sh          # empty payload for the plain kernel
make xkern
make "$KERNEL.iso"              # kernel + init.cpio + grub.cfg -> xkern.iso
cp "$KERNEL.iso" install.img
[ -s install.img ] || { echo "build_full_iso: pass 1 ISO missing" >&2; exit 1; }

echo "== pass 2: embed install.img, rebuild kernel + final ISO"
scripts/gen_payload.sh          # now .incbin's install.img
make xkern                      # relink with the payload embedded
make "$KERNEL.iso"              # final xkern.iso (payload kernel + initramfs)

echo
echo "build_full_iso:"
echo "  install.img  $(wc -c < install.img) bytes  (embedded payload)"
echo "  xkern.iso    $(wc -c < "$KERNEL.iso") bytes  (final bootable ISO)"
echo "  next: qemu-system-x86_64 -cdrom xkern.iso"
