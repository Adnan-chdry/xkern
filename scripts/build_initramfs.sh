#!/bin/sh
#
# build_initramfs.sh - build the XKERN recovery initramfs.
#
# Stages userland content (an /init script plus a hello ELF built with the
# XKERN i386 toolchain) under initrd_stage/ and packs it as a newc cpio
# archive into iso/boot/init.cpio, which GRUB loads as a multiboot module
# (see iso/boot/grub/grub.cfg).
#
# Requirements: cpio, the cc-xkern-i386 toolchain.

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="$ROOT/tools/cc-xkern-i386/cc-xkern-i386"
STAGE="$ROOT/initrd_stage"
OUT="$ROOT/iso/boot/init.cpio"
STAGE_INIT="$STAGE/init"
STAGE_HELLO="$STAGE/bin/hello"
SRC="$ROOT/tools/cc-xkern-i386/examples/hello.c"

rm -rf "$STAGE"
mkdir -p "$STAGE/bin"

# /init script: printed to the VGA console before the userland takes over.
cat > "$STAGE_INIT" <<'EOF'
clear
echo XKERN recovery initram loaded
version
pid
list /
spawn /bin/hello
idle
EOF

# userland hello ELF (spawned by the init script above)
if [ -x "$CC" ]; then
    "$CC" "$SRC" -o "$STAGE_HELLO"
else
    echo "build_initramfs: cc-xkern-i386 toolchain missing, skipping hello" >&2
    sed -i '/spawn \/bin\/hello/d' "$STAGE_INIT"
fi

mkdir -p "$(dirname "$OUT")"
(cd "$STAGE" && find . -type f | sort | cpio -o -H newc > "$OUT")

echo "build_initramfs: $OUT ($(wc -c < "$OUT") bytes)"
