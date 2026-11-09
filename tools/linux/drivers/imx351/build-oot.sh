#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build our IMX351 sensor driver out of tree against the kernel tree (WSL root). The register tables come from a
# header generated LOCALLY from a Droidian CCI trace (camera/cci-trace-header.py; never committed - vendor data).
# usage: build-oot.sh <imx351-camx-regs.h> [out-dir=/root/imx351-build] [ktree=/root/linux]
# -> <out-dir>/imx351.ko (initramfs-update-init.sh picks it up from there for the default boot)
set -e
HDR=$1; O=${2:-/root/imx351-build}; K=${3:-/root/build-duo}; S=$(cd "$(dirname "$0")" && pwd)   # kernel BUILD dir (kernel/build.sh)
[ -f "$HDR" ] || { echo "usage: $0 <imx351-camx-regs.h> [out-dir] [ktree]"; exit 1; }
rm -rf "$O"; mkdir -p "$O"; cp "$S/imx351.c" "$S/Makefile" "$HDR" "$O/"
cd "$O"; sed -i 's/\r$//' imx351.c Makefile "$(basename "$HDR")"
make -C "$K" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M="$O" modules 2>&1 | grep -E "error|warning|LD" | head
echo "imx351.ko: $(modinfo -F vermagic imx351.ko) / params: $(modinfo -F parm imx351.ko | tr '\n' ' ' | cut -c1-80)"
