#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Send one command to the trusted app duo_qsee started, through /sys/kernel/debug/duo_qsee/cmd (root).
# usage: qsee-cmd.py <u32 word> ... [--show N]     e.g.  qsee-cmd.py 0x0c 0x01 0 0 1024 --show 8
# The words are the shared-buffer command (little-endian u32s); prints the transfer status and the first N words of
# the shared buffer after the call (+ any printable text in it).
import struct, sys

D = "/sys/kernel/debug/duo_qsee/cmd"
args = sys.argv[1:]
show = 12
if "--show" in args:
    i = args.index("--show"); show = int(args[i + 1]); del args[i:i + 2]
words = [int(a, 0) & 0xffffffff for a in args]
with open(D, "wb", buffering=0) as f:
    f.write(struct.pack("<%dI" % len(words), *words))
data = open(D, "rb").read()
rsp, sh = data[:64], data[64:]
print("transfer status", struct.unpack("<i", rsp[:4])[0])
w = struct.unpack("<%dI" % show, sh[:4 * show])
print(" ".join("%08x" % x for x in w))
txt = "".join(chr(b) if 32 <= b < 127 else "." for b in sh[:256])
if sum(c.isalnum() for c in txt) > 20:
    print("text:", txt.rstrip("."))
