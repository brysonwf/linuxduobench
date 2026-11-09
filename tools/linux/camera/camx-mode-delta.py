#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Append a mode DELTA to the imx351 driver's LOCAL register header (2026-11-07: 4:3 + square photo modes).
The driver streams init + the 16:9 mode table, then a delta. This computes delta = every register whose value in the
target camx mode table differs from the base 16:9 camx table (first write per register wins, like the tables do),
from the phone's own proprietary camx module -> the output is vendor data: local header only, never commit.
Phone tables (com.qti.sensormodule.sony_imx351.bin, camx-regs.py offsets): 0x51800 = 4032x2268 (base),
0x4e698 = 3712x2784 (4:3), 0x5e040 = 2784x2784 (1:1). PLL/lanes/link are identical in all three (checked).
usage: python3 -I camx-mode-delta.py <camx-regs-output.txt> <base-off> <mode-off> <array-name> <header.h>  (appends;
       idempotent: an existing array of that name is left alone)"""
import re, sys
txt, base, mode, name, hdr = sys.argv[1:6]
tabs, cur = {}, None
for line in open(txt):
    m = re.match(r'# table at (0x[0-9a-f]+)', line)
    if m: cur = m.group(1); tabs[cur] = {}; order = []; continue
    if cur:
        for a, v in re.findall(r'([0-9a-f]{4})=([0-9a-f]{2})', line):
            tabs[cur].setdefault(int(a, 16), int(v, 16))
b, t = tabs[base], tabs[mode]
delta = [(a, v) for a, v in t.items() if a != 0x0100 and b.get(a) != v]
h = open(hdr, encoding='utf-8').read()
if ('%s[]' % name) in h:
    print(name, 'already in', hdr); sys.exit(0)
o = ['', 'static const struct cci_reg_sequence %s[] = {	/* camx %s minus %s (%d regs) */' % (name, mode, base, len(delta))]
o += ['\t{ CCI_REG8(0x%04x), 0x%02x },' % (a, v) for a, v in delta]
o += ['};', '#define %s_PRESENT 1' % name.upper(), '']
open(hdr, 'a', encoding='utf-8', newline='\n').write('\n'.join(o))
print('appended', name, len(delta), 'registers to', hdr)
