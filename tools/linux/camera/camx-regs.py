#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract register tables from a Qualcomm CamX sensor-module binary ("QTI Chromatix Header", compiled from the
chi-cdk <sensor>_sensor.xml) - camera step 2, docs/research/33 "CAMERA". Reads the phone's OWN file (vendor_a
lib64/camera/com.qti.sensormodule.*.bin, copied to artifacts/local - proprietary, never committed) and prints the
regSetting tables, as reverse-engineered on 2026-11-04 (IMX351 module):
  entry  = u64 words: addr, dataCount(1), id, addrType(1=byte,2=word), dataType, operation(0=write), hasDelay, id+1,
           delayUs, id+2  (80 bytes; the table's LAST entry stops after id+1 = 64 bytes)
  pool   = right after the last entry: one 16-byte slot per entry, u64 value first -> the register data, in order
Before each mode table: width, height (u64) a few words back (e.g. 0xfc0 x 0xae0 = 4032x2784).
usage: python3 -I camx-regs.py <sensormodule.bin> [min-table-len=4]"""
import sys, struct
d = open(sys.argv[1], 'rb').read(); minlen = int(sys.argv[2]) if len(sys.argv) > 2 else 4
n = len(d) // 8; w = struct.unpack_from('<%dQ' % n, d, 0)

def head(k):	# the common first 8 words of an entry
    if k + 8 > n: return False
    a = w[k:k + 8]
    return a[0] <= 0xffff and a[1] == 1 and a[3] in (1, 2) and a[4] in (1, 2) and a[5] <= 3 and a[6] <= 1 and a[7] == a[2] + 1

tables = []; k = 0
while k < n:
    if not head(k): k += 1; continue
    ents = []; j = k
    while head(j):
        ents.append(j)
        if j + 10 <= n and w[j + 9] == w[j + 2] + 2 and head(j + 10): j += 10
        else: break
    last = ents[-1]; pool = last + 8
    if len(ents) >= minlen and pool + 2 * len(ents) <= n:
        regs = [(w[e], w[pool + 2 * i], w[e + 4], w[e + 8] if e != last else 0) for i, e in enumerate(ents)]
        size = None
        for b in range(k - 16, k - 2):
            if 64 <= w[b] <= 8192 and 64 <= w[b + 1] <= 8192: size = (w[b], w[b + 1]); break
        tables.append((k * 8, size, regs))
    k = pool + 2 * len(ents)

for off, size, regs in tables:
    print('# table at 0x%x: %d writes%s' % (off, len(regs), ' (mode %dx%d)' % size if size else ''))
    print(' '.join('%04x=%02x%s' % (a, v, '+%dus' % dl if dl else '') for a, v, dt, dl in regs))
print('# %d tables' % len(tables))
