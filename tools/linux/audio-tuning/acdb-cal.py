#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""List the ADSP calibration a Qualcomm "QCMSNDDB" ACDB file holds for one device (docs/research/workbench/
adsp-calibration.md): which DSP modules/params Android would program and the size of each parameter block.
Table layout (reverse-engineered 2026-11-07 on the Surface Duo's own files):
  AGIILUT0  device (COPP) cal: rows of u32 [acdb_dev_id, sample_rate, app_type, cdft_off, cdot_off]
  AFEILUT0  AFE port cal:      rows of u32 [acdb_dev_id, sample_rate, cdft_off, cdot_off]
  AVIILUT0  volume-step cal:   rows of u32 [acdb_dev_id, app_type, vol_index, cdft_off, cdot_off]
  xxxxCDFT  at cdft_off: u32 count, then count x [module_id, instance, param_id]
  xxxxCDOT  at cdot_off: u32 count, then count x [offset into DATAPOOL]
  DATAPOOL  at offset: u32 size, then the parameter payload
usage: acdb-cal.py <file.acdb> <acdb_dev_id> [sample_rate=48000] [app_type=0x11130] [--step N] [--dump DIR]
                   [--copp-blob FILE] [--afe-blob FILE] [--gain-db DB]
  --dump writes each parameter block as DIR/<table>-<module>-<instance>-<param>.bin (keep it local: vendor data)
  --step N picks the volume-step (AVII) row, default 0
  --copp-blob / --afe-blob write the device + volume-step blocks (COPP) or the AFE blocks as one ADSP set-param
  payload: repeated [u32 module, u32 param, u16 size, u16 0, data padded to 4] (ADM_CMD_SET_PP_PARAMS_V5 /
  AFE_PORT_CMD_SET_PARAM_V2 in-band format). Vendor data: keep the output local.
  --gain-db DB scales the tuning's gain stage (module 0x10C35, param 0x10C36: Q13 linear gain, after the MBDRC) by
  DB decibels in the COPP blob: makeup gain for the headroom the MBDRC's limiter leaves (the Surface Duo speaker:
  +8, chosen by a listening test, docs/research/workbench/adsp-calibration.md).
"""
import os, struct, sys

def chunks(d):
    off, c = 32, {}
    while off + 12 <= len(d):
        t = d[off:off + 8].decode().strip(); (n,) = struct.unpack_from("<I", d, off + 8)
        c[t] = d[off + 12: off + 12 + n]; off += 12 + n
    return c

def rows(b, w):
    (cnt,) = struct.unpack_from("<I", b, 0)
    return [struct.unpack_from("<%dI" % w, b, 4 + i * w * 4) for i in range(cnt)]

def entries(c, prefix, cdft_off, cdot_off):
    ft, ot, pool = c[prefix + "CDFT"], c[prefix + "CDOT"], c["DATAPOOL"]
    (n,) = struct.unpack_from("<I", ft, cdft_off)
    (m,) = struct.unpack_from("<I", ot, cdot_off)
    out = []
    for i in range(n):
        mod, inst, par = struct.unpack_from("<III", ft, cdft_off + 4 + 12 * i)
        po = struct.unpack_from("<I", ot, cdot_off + 4 + 4 * i)[0] if i < m else None
        blob = b""
        if po is not None and po + 4 <= len(pool):
            (sz,) = struct.unpack_from("<I", pool, po); blob = pool[po + 4: po + 4 + sz]
        out.append((mod, inst, par, blob))
    return out

def opt(name, conv=str):
    if name not in sys.argv:
        return None
    i = sys.argv.index(name); v = sys.argv[i + 1]; del sys.argv[i:i + 2]
    return conv(v)
dump = opt("--dump"); step = opt("--step", lambda v: int(v, 0)) or 0
copp_blob = opt("--copp-blob"); afe_blob = opt("--afe-blob")
gain_db = opt("--gain-db", float) or 0.0
a = [x for x in sys.argv[1:] if not x.startswith("--")]
f, dev = a[0], int(a[1], 0)
rate = int(a[2], 0) if len(a) > 2 else 48000
app = int(a[3], 0) if len(a) > 3 else 0x11130
c = chunks(open(f, "rb").read())

def setparam(blocks):
    out = b""
    for mod, par, blob in blocks:
        out += struct.pack("<IIHH", mod, par, len(blob), 0) + blob + bytes(-len(blob) % 4)
    return out

def show(name, prefix, sel, w, offs):
    got = []
    hits = [r for r in rows(c[prefix + "LUT0"], w) if sel(r)]
    print("== %s: %d matching rows" % (name, len(hits)))
    for r in hits[:1]:
        cf, co = offs(r)
        for mod, inst, par, blob in entries(c, prefix, cf, co):
            print("   module 0x%08x inst %d param 0x%08x  %5d bytes  %s" % (mod, inst, par, len(blob), blob[:16].hex(" ")))
            got.append((mod, par, blob))
            if dump:
                os.makedirs(dump, exist_ok=True)
                open("%s/%s-%08x-%d-%08x.bin" % (dump, prefix, mod, inst, par), "wb").write(blob)
    return got

dev_blocks = show("device (COPP) dev %d rate %d app 0x%x" % (dev, rate, app), "AGII",
     lambda r: r[0] == dev and r[1] == rate and r[2] == app, 5, lambda r: (r[3], r[4]))
afe_blocks = show("AFE port dev %d rate %d" % (dev, rate), "AFEI", lambda r: r[0] == dev and r[1] == rate, 4,
     lambda r: (r[2], r[3]))
vol_blocks = show("volume step %d dev %d app 0x%x" % (step, dev, app), "AVII",
     lambda r: r[0] == dev and r[1] == app and r[2] == step, 5, lambda r: (r[3], r[4]))
def with_gain(blocks, db):
    out = []
    for mod, par, blob in blocks:
        if db and (mod, par) == (0x10C35, 0x10C36):
            (g,) = struct.unpack_from("<H", blob, 0)
            ng = min(0xFFFF, round(g * 10 ** (db / 20)))
            print("gain 0x%05x/0x%05x: 0x%04x -> 0x%04x (%+.1f dB)" % (mod, par, g, ng, db))
            blob = struct.pack("<H", ng) + blob[2:]
        out.append((mod, par, blob))
    return out

for path, blocks in ((copp_blob, with_gain(dev_blocks + vol_blocks, gain_db)), (afe_blob, afe_blocks)):
    if path:
        data = setparam(blocks); open(path, "wb").write(data)
        print("wrote %s: %d blocks, %d bytes" % (path, len(blocks), len(data)))
