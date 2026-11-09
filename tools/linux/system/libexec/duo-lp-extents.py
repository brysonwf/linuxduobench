#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# duo-lp-extents - where a logical partition (vendor_a, system_a, ...) lives inside Android's "super" partition.
# Reads super's own metadata (AOSP liblp format, metadata_format.h: geometry at 4096, primary metadata slot 0 at
# 4096 + 2 * 4096) instead of a hard-coded offset, so it works on any Duo's super layout.
# usage: duo-lp-extents.py <super block device or image> <partition name>
# prints one line per extent: "<start sector> <number of sectors>" (512-byte sectors, linear extents), exit 1 if absent
import struct
import sys

GEOMETRY_MAGIC = 0x616C4467
HEADER_MAGIC = 0x414C5030
RESERVED = 4096


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: duo-lp-extents.py <super> <partition>")
    dev, want = sys.argv[1], sys.argv[2]
    with open(dev, "rb") as f:
        f.seek(RESERVED)
        geo = f.read(52)
        magic, _size = struct.unpack_from("<II", geo, 0)
        if magic != GEOMETRY_MAGIC:
            sys.exit("no super metadata (geometry magic %#x)" % magic)
        max_size, slots, _lbs = struct.unpack_from("<III", geo, 40)
        f.seek(RESERVED + 2 * 4096)   # primary metadata, slot 0
        hdr = f.read(256)
        magic, major, minor, hsize = struct.unpack_from("<IHHI", hdr, 0)
        if magic != HEADER_MAGIC:
            sys.exit("bad metadata header magic %#x" % magic)
        # after header_checksum[32]: tables_size, tables_checksum[32], then 4 table descriptors (offset, count, size)
        tables = 12 + 32 + 4 + 32
        desc = [struct.unpack_from("<III", hdr, tables + 12 * i) for i in range(4)]
        f.seek(RESERVED + 2 * 4096 + hsize)
        body = f.read(desc[3][0] + desc[3][1] * desc[3][2] + 4096)
    (poff, pnum, psz), (eoff, enum_, esz) = desc[0], desc[1]
    for i in range(pnum):
        e = body[poff + i * psz: poff + (i + 1) * psz]
        name = e[:36].split(b"\0")[0].decode()
        if name != want:
            continue
        _attrs, first, count = struct.unpack_from("<III", e, 36)
        for j in range(first, first + count):
            x = body[eoff + j * esz: eoff + (j + 1) * esz]
            sectors, ttype, start, src = struct.unpack_from("<QIQI", x, 0)
            if ttype != 0 or src != 0:
                sys.exit("extent %d of %s is not a plain linear extent on super" % (j, want))
            print(start, sectors)
        return
    sys.exit("no logical partition '%s' in %s (metadata v%d.%d)" % (want, dev, major, minor))


main()
