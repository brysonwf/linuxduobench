#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""List the custom ADSP topologies (id + module list) a Qualcomm QCMSNDDB GLOBAL acdb file defines: global props
0x12E47 (ADM/ASM), 0x130DE (AFE), 0x12E48 (other); 392-byte slots = [id, n, 16 x 24-byte module records].
usage: acdb-topologies.py <..._Global_cal.acdb> [--avcs-blob FILE]  (docs/research/workbench/adsp-calibration.md)
  --avcs-blob writes global prop 0x132AC unchanged (what libaudcal's AcdbCmdGetAVCSCustomTopoInfo_V3 returns):
  the payload of AVCS_CMD_REGISTER_TOPOLOGIES (u32 count, then [2, id, type, n, n x (module, 0x10000)] records),
  which the kernel loads as qcom/sm8150/microsoft/avcs-topologies.bin. Vendor data: keep the output local."""
import struct, subprocess, sys
import os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "acdb-dump.py")
avcs = None
if "--avcs-blob" in sys.argv:
    i = sys.argv.index("--avcs-blob"); avcs = sys.argv[i + 1]; del sys.argv[i:i + 2]
f = sys.argv[1]
x = lambda t: subprocess.run([sys.executable, "-I", D, f, "-x", t], capture_output=True).stdout
pool, prop = x("DATAPOOL"), x("GPROPLUT")
props = {}
for i in range(struct.unpack_from("<I", prop, 0)[0]):
    pid, off = struct.unpack_from("<II", prop, 4 + 8 * i)
    sz = struct.unpack_from("<I", pool, off)[0]; props[pid] = pool[off + 4: off + 4 + sz]
if avcs:
    b = props[0x132AC]
    open(avcs, "wb").write(b)
    print("wrote %s: %d bytes, %d topologies" % (avcs, len(b), struct.unpack_from("<I", b, 0)[0]))
    sys.exit(0)
for pid, what in ((0x12E47, "ADM/ASM topologies"), (0x130DE, "AFE topologies"), (0x12E48, "other")):
    b = props[pid]; n = struct.unpack_from("<I", b, 0)[0]
    stride = (len(b) - 4) // n if (len(b) - 4) % n == 0 else None
    print("== prop 0x%x (%s): %d topologies, %s bytes each" % (pid, what, n, stride))
    if not stride: continue
    for t in range(n):
        o = 4 + t * stride
        topo, nm = struct.unpack_from("<II", b, o)
        mods = [struct.unpack_from("<I", b, o + 8 + 24 * m)[0] for m in range(nm)]
        flag = "  <== speaker" if topo in (0x10010313, 0x1001025e, 0x1001025d) else ""
        print("   0x%08x %2d: %s%s" % (topo, nm, " ".join("%x" % m for m in mods), flag))
