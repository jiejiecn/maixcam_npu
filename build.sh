#!/bin/sh
# build.sh - build npu_freq.ko against a Linux 5.10.4 kernel tree
#
#   ./build.sh /path/to/linux-5.10.4 [riscv64-linux-gnu-]
#
# The kernel tree must be configured for the device:
#   - 5.10.4 with EXTRAVERSION = -tag-
#   - .config imported from the device:  zcat /proc/config.gz > .config
#   - modules_prepare done once:
#       make ARCH=riscv CROSS_COMPILE=... olddefconfig modules_prepare
set -e

KDIR=${1:-}
CROSS=${2:-riscv64-linux-gnu-}
HERE=$(cd "$(dirname "$0")" && pwd)

if [ -z "$KDIR" ]; then
    echo "usage: $0 <kernel-tree> [cross-prefix]" >&2
    exit 1
fi
if [ ! -f "$KDIR/Makefile" ]; then
    echo "error: $KDIR does not look like a kernel tree" >&2
    exit 1
fi

make -C "$KDIR" M="$HERE/src" ARCH=riscv CROSS_COMPILE="$CROSS" modules

mkdir -p "$HERE/prebuilt"
cp "$HERE/src/npu_freq.ko" "$HERE/prebuilt/npu_freq.ko"

echo
echo "built: $HERE/prebuilt/npu_freq.ko"
echo "vermagic:"
grep -a -o 'vermagic=[^ ]*' "$HERE/prebuilt/npu_freq.ko" || true
echo "expected on device: 5.10.4-tag- preempt mod_unload riscv64"
