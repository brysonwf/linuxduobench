#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# The penguin is inspired by Tux, created by Larry Ewing with The GIMP (original art, not a copy; bootanim/CREDITS.md).
# Build the boot animation player + its frame set (WSL root).
# usage: build-bootanim.sh <repo> <art-dir: ".../linux-duo-bench-boot-art"> <out-dir>
#   <out-dir>/duo-bootanim            static arm64 binary
#   <out-dir>/frames/*.ppm + seq.txt  the art's loop/ frames (900x675 P6) and the sequence as plain text
#   <out-dir>/bootanim.cpio.gz        frames (usr/share/duo/bootanim/) + player (usr/local/bin/duo-bootanim) as a 2nd
#                                     initrd (initrd=/duo/bootanim.gz); /init plays it when present, else the old splash
# The art is kept out of git (170 MB of frames); its generator lives with the art (scripts/build.mjs). Kernel uapi
# headers: make -C /root/linux ARCH=arm64 headers_install INSTALL_HDR_PATH=/root/hdr (as for drmfill).
set -e
REPO=$1; ART=$2; OUT=$3; S=$REPO/tools/linux/bootanim
mkdir -p $OUT/frames
[ -d /root/hdr/include/drm ] || make -C /root/linux ARCH=arm64 headers_install INSTALL_HDR_PATH=/root/hdr > /dev/null
tr -d '\r' < $S/duo-bootanim.c > /tmp/duo-bootanim.c
aarch64-linux-gnu-gcc -static -O2 -Wall -I/root/hdr/include -o $OUT/duo-bootanim /tmp/duo-bootanim.c
cp $ART/loop/*.ppm $OUT/frames/
python3 -I - "$ART/loop/sequence.json" "$OUT/frames/seq.txt" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
with open(sys.argv[2], "w") as f:
    f.write("fps %d\n" % d["fps"])
    for s in d["loop"]:
        f.write("L %s %s\n" % (s["top"], s["bottom"]))
    f.write("E %s %s\n" % (d["end"]["top"], d["end"]["bottom"]))
print("seq.txt: %d loop frames + end" % len(d["loop"]))
EOF
cp $ART/CREDITS.md $OUT/frames/CREDITS.md
T=$(mktemp -d); mkdir -p $T/usr/share/duo/bootanim $T/usr/local/bin; cp $OUT/frames/* $T/usr/share/duo/bootanim/
cp $OUT/duo-bootanim $T/usr/local/bin/duo-bootanim; chmod 755 $T/usr/local/bin/duo-bootanim
# paths only, NEVER "." (2026-11-07: mktemp's 0700 "." entry, unpacked over pmOS's / with cpio -idmu, made / 0700 -
# no user could log in, Phosh failed) - and explicit modes/owner on every entry
chmod -R u=rwX,go=rX $T/usr; chown -R 0:0 $T/usr
(cd $T && find usr | cpio -o -H newc -R 0:0 2>/dev/null) | gzip -9 > $OUT/bootanim.cpio.gz; rm -rf $T
ls -la $OUT/duo-bootanim $OUT/bootanim.cpio.gz; md5sum $OUT/duo-bootanim $OUT/bootanim.cpio.gz
