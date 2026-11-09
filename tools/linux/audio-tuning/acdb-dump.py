#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Map the structure of a Qualcomm "QCMSNDDB" ACDB file (the audio calibration database Android loads into the ADSP;
the Duo's own copies live on vendor_a /etc/acdbdata/Surface/). Reverse-engineering aid for the ADSP calibration work
(docs/research/workbench/adsp-calibration.md) - no vendor data is ever stored in the repo, only what we learn.
Layout seen so far: 8-byte magic "QCMSNDDB", 8 bytes, then a container "AVDB" with two u32 sizes, then chunks of
<8-byte ASCII tag><u32 length><payload>. Prints every chunk (tag, offset, length, a short preview); -x TAG dumps one.
usage: acdb-dump.py <file.acdb> [-x TAG]
"""
import struct, sys

def is_tag(b):
    return len(b) == 8 and all(32 <= c < 127 for c in b)

def preview(p):
    s = p[:48]
    try:
        u = s.decode("utf-16-le")
        if u.isprintable() and len(u) > 2:
            return '"' + u + '"'
    except Exception:
        pass
    return s.hex(" ")

def walk(d, off, end, depth, xtag):
    while off + 8 <= end:
        tag = d[off:off + 8]
        if not is_tag(tag):
            print("  " * depth + "?? non-tag at 0x%x: %s" % (off, d[off:off + 16].hex(" ")))
            return
        (n,) = struct.unpack_from("<I", d, off + 8)
        body = d[off + 12: off + 12 + n]
        t = tag.decode()
        if xtag and t.strip() == xtag:
            sys.stdout.buffer.write(body); return
        if not xtag:
            print("%s%-8s @0x%06x len %7d  %s" % ("  " * depth, t, off, n, preview(body)))
        off += 12 + n

d = open(sys.argv[1], "rb").read()
xtag = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == "-x" else None
assert d[:8] == b"QCMSNDDB", "not a QCMSNDDB file"
if not xtag:
    print("magic QCMSNDDB, then %s" % d[8:16].hex(" "))
# container header: "AVDB" (device files) / "GCDB" (global file) ... + three u32
ctag = d[16:20].decode()
assert ctag.isalnum(), "unexpected container tag %r" % d[16:20]
a, b, c = struct.unpack_from("<III", d, 20)
if not xtag:
    print("%s %d %d %d (file %d)" % (ctag, a, b, c, len(d)))
walk(d, 32, len(d), 0, xtag)
