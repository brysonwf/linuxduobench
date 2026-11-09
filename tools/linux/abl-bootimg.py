#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build an Android boot.img (header v2) that the Duo's ABL can RAM-boot directly (fastboot boot), skipping the SB-off
UEFI + GRUB path - for A/B tests of the boot path (docs/research/33 "MODEM round 5": EFS/TFTP/HW MBN/regulators/pins all
match Droidian, whose kernel ABL boots directly). Load addresses / page size / OS version are copied from a reference
boot.img (Droidian's), so ABL sees the layout it expects. The DTB must carry qcom,msm-id/board-id (ABL picks a DTB by
them; Droidian's: <0x153 0x20000> / <0 0>).
usage: python3 -I abl-bootimg.py <reference-boot.img> <kernel Image.gz> <ramdisk.cpio.gz> <dtb> "<cmdline>" <out.img>"""
import hashlib, struct, sys

ref, kern, rd, dtbf, cmdline, out = sys.argv[1:7]
r = open(ref, 'rb').read(4096)
assert r[:8] == b'ANDROID!' and struct.unpack('<I', r[40:44])[0] == 2, 'reference must be a header v2 boot.img'
kernel_addr, = struct.unpack('<I', r[12:16]); ramdisk_addr, = struct.unpack('<I', r[20:24])
second_addr, = struct.unpack('<I', r[28:32]); tags_addr, = struct.unpack('<I', r[32:36])
page, = struct.unpack('<I', r[36:40]); os_version, = struct.unpack('<I', r[44:48])
dtb_addr, = struct.unpack('<Q', r[1652:1660])
# A mainline Image decompresses to ~78 MB, far past Droidian's ramdisk (+16 MB) / dtb (+31 MB) offsets: ABL then refused
# to start it (2026-11-05: stuck on the logo, back to fastboot). RAMDISK_ADDR / DTB_ADDR (hex, relative like the header
# values) move them clear of the kernel.
import os
ramdisk_addr = int(os.environ.get('RAMDISK_ADDR', hex(ramdisk_addr)), 16)
dtb_addr = int(os.environ.get('DTB_ADDR', hex(dtb_addr)), 16)
print('addrs: kernel', hex(kernel_addr), 'ramdisk', hex(ramdisk_addr), 'dtb', hex(dtb_addr), 'tags', hex(tags_addr))
K = open(kern, 'rb').read(); R = open(rd, 'rb').read(); D = open(dtbf, 'rb').read()
c = cmdline.encode()
assert len(c) < 512 + 1024, 'cmdline too long'
main, extra = c[:511], c[511:]

sha = hashlib.sha1()
for blob in (K, R, b'', b'', D):	# kernel, ramdisk, second, recovery_dtbo, dtb (+ sizes), like mkbootimg
    sha.update(blob); sha.update(struct.pack('<I', len(blob)))
hdr = struct.pack('<8s10I16s512s32s1024sIQIIQ', b'ANDROID!', len(K), kernel_addr, len(R), ramdisk_addr, 0, second_addr,
                  tags_addr, page, 2, os_version, b'', main, sha.digest(), extra, 0, 0, 1660, len(D), dtb_addr)
pad = lambda b: b + b'\0' * ((page - len(b) % page) % page)
with open(out, 'wb') as f:
    f.write(pad(hdr)); f.write(pad(K)); f.write(pad(R)); f.write(pad(D))
print('wrote', out, 'kernel', len(K), 'ramdisk', len(R), 'dtb', len(D), 'page', page)
