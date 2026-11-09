#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build an Android boot image the Duo's own bootloader (ABL) starts with `fastboot boot` - no UEFI, no ESP
# (docs/research/workbench/no-windows-layout.md; the 2026-11-05 findings in modem.md "ABL direct boot").
# ABL's conditions, all handled here:
#   - kernel < ~60 MiB decompressed (image_size in the Image header; a Qualcomm-only kernel is ~54.5 MiB)
#   - ramdisk well below ~52 MB (use the small abl-stage1 ramdisk; it loads the full initramfs from the phone)
#   - DTB: the RAM node named exactly /memory (ABL fills it in), and ABL must be able to apply the board's dtbo overlay
#     -> an "overlay sink": a disabled node with one empty child per label the overlay references + __symbols__
#       pointing there (the overlay then writes only into the disabled sink)
# usage (WSL root): make-abl-image.sh <duo.dtb> <Image.gz> <ramdisk.cpio.gz> <reference-boot.img> <out.img> ["cmdline"]
#   reference-boot.img = any boot image the phone's ABL accepts (header v2 + addresses are copied from it)
#   DTBO=<board dtbo> (e.g. dt31.dtbo carved from your phone's dtbo partition) = labels + a merge check; without it the
#   labels list from the 2026-11-05 run (tools/linux/abl/dt31-fixup-labels.txt) is used and no merge check is done
set -e
DTB=$1; KER=$2; RD=$3; REF=$4; OUT=$5
CMD=${6:-"rdinit=/init clk_ignore_unused pd_ignore_unused console=tty0 loglevel=7 panic=10 fbcon=font:TER16x32 fbcon=rotate:1 consoleblank=0"}
H=$(cd "$(dirname "$0")" && pwd); W=$(mktemp -d)
for f in "$DTB" "$KER" "$RD" "$REF"; do [ -s "$f" ] || { echo "missing $f"; exit 1; }; done
# kernel size check (image_size at offset 16 of the arm64 Image header)
zcat "$KER" | head -c 64 > $W/hdr
SZ=$(python3 -I -c "import struct,sys; print(struct.unpack('<Q', open(sys.argv[1],'rb').read()[16:24])[0])" $W/hdr)
echo "kernel image_size $((SZ / 1048576)) MiB"; [ $SZ -lt $((60 * 1048576)) ] || { echo "too big for ABL (>= 60 MiB): build a Qualcomm-only kernel"; exit 1; }
RS=$(stat -c %s "$RD"); [ $RS -lt $((40 * 1048576)) ] || { echo "ramdisk $((RS / 1048576)) MB: too big for ABL, use abl-stage1"; exit 1; }
# /memory rename
dtc -q -I dtb -O dts -o $W/base.dts "$DTB"
sed -i 's/^\tmemory@[0-9a-f]* {/\tmemory {/' $W/base.dts
grep -q $'^\tmemory {' $W/base.dts || { echo "no top-level memory node renamed"; exit 1; }
# labels the board overlay references
if [ -n "$DTBO" ]; then fdtget -p "$DTBO" /__fixups__ > $W/labels.txt; else cp $H/dt31-fixup-labels.txt $W/labels.txt; fi
python3 -I - $W/base.dts $W/labels.txt $W/sink.dts <<'PY'
import re, sys
s = open(sys.argv[1]).read(); labels = open(sys.argv[2]).read().split()
# the UEFI framebuffer node: ABL leaves no framebuffer there (its memory map differs), so it could never claim it
s, n = re.subn(r'\n\t+framebuffer@[0-9a-f]+ \{\n\t+compatible = "simple-framebuffer";.*?\n\t+\};', '', s, flags=re.S)
print(n, 'simple-framebuffer node(s) dropped')
ph = max(int(x, 16) for x in re.findall(r'phandle = <0x([0-9a-f]+)>', s)) + 1
sink = '\n\tduo-abl-overlay-sink {\n\t\tstatus = "disabled";\n'
syms = '\n\t__symbols__ {\n'
for i, l in enumerate(labels):
    sink += '\t\t%s {\n\t\t\tphandle = <0x%x>;\n\t\t};\n' % (l, ph + i)
    syms += '\t\t%s = "/duo-abl-overlay-sink/%s";\n' % (l, l)
i = s.rindex('};')
open(sys.argv[3], 'w').write(s[:i] + sink + '\t};\n' + syms + '\t};\n' + s[i:])
print(len(labels), 'overlay labels sunk')
PY
dtc -q -I dts -O dtb -o $W/abl.dtb $W/sink.dts
[ -n "$DTBO" ] && { fdtoverlay -i $W/abl.dtb -o $W/merged.dtb "$DTBO" && echo "board dtbo merges cleanly (as ABL would)"; }
python3 -I $H/../abl-bootimg.py "$REF" "$KER" "$RD" $W/abl.dtb "$CMD" "$OUT" | tail -1
ls -la "$OUT" | awk '{printf "%s %.1f MB\n", $9, $5/1048576}'; rm -rf $W
