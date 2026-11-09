#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Surface Duo workbench boot logo (a really fat penguin splash loading logo,
potentially with a workbench behind him and a lot of daily carry stuff on him). Original art drawn from simple shapes
by this script (no image libraries): a very fat penguin in front of a workbench + pegboard, wearing a headlamp, a
utility belt (pouches, multitool, carabiner with keys) and a watch, holding a flashlight and a little Surface Duo.
Writes the kernel's logo format (plain P3 PPM, <= 224 colours, CONFIG_LOGO_LINUX_CLUT224) and a PNG preview.
usage: python3 -I make-penguin-logo.py <out.ppm> [preview.png] [width=320] [height=260]"""
import math, struct, sys, zlib

OUT = sys.argv[1]; PNG = sys.argv[2] if len(sys.argv) > 2 else None
W = int(sys.argv[3]) if len(sys.argv) > 3 else 320; H = int(sys.argv[4]) if len(sys.argv) > 4 else 260
SS = 4                                  # supersampling per axis (anti-aliased edges)
cw, ch = W * SS, H * SS
img = [[(0, 0, 0)] * cw for _ in range(ch)]

def S(v): return int(round(v * SS))
def put(x, y, c):
    if 0 <= x < cw and 0 <= y < ch: img[y][x] = c
def rect(x0, y0, x1, y1, c):
    for y in range(max(0, S(y0)), min(ch, S(y1))):
        row = img[y]
        for x in range(max(0, S(x0)), min(cw, S(x1))): row[x] = c
def ellipse(cx, cy, rx, ry, c, rot=0.0):
    cr, sr = math.cos(rot), math.sin(rot); R = max(rx, ry)
    for y in range(max(0, S(cy - R)), min(ch, S(cy + R) + 1)):
        for x in range(max(0, S(cx - R)), min(cw, S(cx + R) + 1)):
            dx, dy = x / SS - cx, y / SS - cy
            u, v = dx * cr + dy * sr, -dx * sr + dy * cr
            if (u / rx) ** 2 + (v / ry) ** 2 <= 1: img[y][x] = c
def poly(pts, c):
    ys = [p[1] for p in pts]
    for y in range(max(0, S(min(ys))), min(ch, S(max(ys)) + 1)):
        fy = (y + 0.5) / SS; xs = []
        for i in range(len(pts)):
            (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
            if (y0 <= fy < y1) or (y1 <= fy < y0): xs.append(x0 + (fy - y0) * (x1 - x0) / (y1 - y0))
        xs.sort()
        for a, b in zip(xs[0::2], xs[1::2]):
            for x in range(max(0, S(a)), min(cw, S(b) + 1)): img[y][x] = c
def line(x0, y0, x1, y1, w, c):
    n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
    for i in range(n + 1):
        t = i / n; ellipse(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, w / 2, w / 2, c)

# palette (kept small so the 224-colour limit holds after anti-aliasing)
BLACK, INK, BELLY, BEAK, FEET = (0, 0, 0), (24, 26, 34), (238, 238, 230), (250, 160, 30), (238, 128, 20)
WOOD, WOOD_D, PEG, PEG_D = (150, 96, 52), (104, 64, 34), (196, 160, 110), (150, 118, 78)
STEEL, STEEL_D, RED, BLUE, GREEN = (180, 186, 194), (110, 116, 126), (210, 50, 50), (60, 110, 210), (70, 160, 80)
BELT, POUCH, LIGHT, BEAM, DUO = (90, 60, 36), (70, 92, 52), (255, 240, 150), (255, 250, 200), (220, 220, 225)
EYE_W, GLASS = (255, 255, 255), (40, 120, 200)

cx = W * 0.5
# --- workbench + pegboard behind him
rect(W * 0.06, H * 0.10, W * 0.94, H * 0.52, PEG)
for gx in range(int(W * 0.08), int(W * 0.94), 14):
    for gy in range(int(H * 0.13), int(H * 0.50), 14): ellipse(gx, gy, 1.2, 1.2, PEG_D)
line(W * 0.12, H * 0.18, W * 0.22, H * 0.40, 5, STEEL); ellipse(W * 0.12, H * 0.18, 7, 7, STEEL); ellipse(W * 0.12, H * 0.18, 3.5, 3.5, PEG)  # wrench
rect(W * 0.78, H * 0.16, W * 0.81, H * 0.30, RED); line(W * 0.795, H * 0.30, W * 0.795, H * 0.43, 2.5, STEEL)   # screwdriver
rect(W * 0.85, H * 0.18, W * 0.90, H * 0.24, STEEL_D); line(W * 0.875, H * 0.24, W * 0.875, H * 0.42, 3, WOOD)  # hammer
ellipse(W * 0.30, H * 0.22, 9, 9, GREEN); ellipse(W * 0.30, H * 0.22, 4, 4, PEG)                                 # tape roll
rect(W * 0.02, H * 0.52, W * 0.98, H * 0.60, WOOD); rect(W * 0.02, H * 0.58, W * 0.98, H * 0.61, WOOD_D)          # bench top
rect(W * 0.06, H * 0.61, W * 0.10, H * 0.98, WOOD_D); rect(W * 0.90, H * 0.61, W * 0.94, H * 0.98, WOOD_D)        # legs

# --- the (very) fat penguin
ellipse(cx, H * 0.62, W * 0.27, H * 0.36, INK)                   # body
ellipse(cx, H * 0.66, W * 0.20, H * 0.29, BELLY)                 # belly
ellipse(cx, H * 0.27, W * 0.15, H * 0.16, INK)                   # head
ellipse(cx - W * 0.05, H * 0.27, W * 0.045, H * 0.06, EYE_W); ellipse(cx + W * 0.05, H * 0.27, W * 0.045, H * 0.06, EYE_W)
ellipse(cx - W * 0.042, H * 0.285, W * 0.02, H * 0.03, BLACK); ellipse(cx + W * 0.058, H * 0.285, W * 0.02, H * 0.03, BLACK)
poly([(cx - W * 0.05, H * 0.34), (cx + W * 0.05, H * 0.34), (cx, H * 0.40)], BEAK)
# headlamp: strap + lamp + beam
rect(cx - W * 0.15, H * 0.17, cx + W * 0.15, H * 0.20, BELT); ellipse(cx, H * 0.17, W * 0.035, H * 0.035, STEEL)
ellipse(cx, H * 0.17, W * 0.02, H * 0.02, LIGHT); poly([(cx - 4, H * 0.13), (cx + 4, H * 0.13), (cx + 24, 0), (cx - 24, 0)], BEAM)
# flippers
ellipse(cx - W * 0.27, H * 0.58, W * 0.06, H * 0.17, INK, rot=0.45); ellipse(cx + W * 0.27, H * 0.58, W * 0.06, H * 0.17, INK, rot=-0.45)
# left flipper: flashlight with beam
rect(cx - W * 0.40, H * 0.66, cx - W * 0.30, H * 0.70, STEEL_D); rect(cx - W * 0.42, H * 0.655, cx - W * 0.40, H * 0.705, STEEL)
poly([(cx - W * 0.42, H * 0.66), (cx - W * 0.42, H * 0.70), (cx - W * 0.50, H * 0.76), (cx - W * 0.50, H * 0.60)], BEAM)
# right flipper: a little Surface Duo (two panels + hinge)
rect(cx + W * 0.29, H * 0.60, cx + W * 0.345, H * 0.70, DUO); rect(cx + W * 0.35, H * 0.60, cx + W * 0.405, H * 0.70, DUO)
rect(cx + W * 0.30, H * 0.61, cx + W * 0.338, H * 0.69, GLASS); rect(cx + W * 0.357, H * 0.61, cx + W * 0.395, H * 0.69, GLASS)
rect(cx + W * 0.345, H * 0.60, cx + W * 0.35, H * 0.70, STEEL_D)
# watch on the left flipper
rect(cx - W * 0.31, H * 0.52, cx - W * 0.27, H * 0.555, BELT); ellipse(cx - W * 0.29, H * 0.537, W * 0.016, H * 0.02, STEEL)
# utility belt: belt, buckle, pouches, multitool, carabiner + keys
rect(cx - W * 0.235, H * 0.74, cx + W * 0.235, H * 0.79, BELT)
rect(cx - W * 0.025, H * 0.735, cx + W * 0.025, H * 0.795, STEEL); rect(cx - W * 0.012, H * 0.75, cx + W * 0.012, H * 0.78, BELT)
rect(cx - W * 0.20, H * 0.79, cx - W * 0.12, H * 0.87, POUCH); rect(cx - W * 0.20, H * 0.79, cx - W * 0.12, H * 0.81, BELT)
rect(cx + W * 0.12, H * 0.79, cx + W * 0.20, H * 0.87, POUCH); rect(cx + W * 0.12, H * 0.79, cx + W * 0.20, H * 0.81, BELT)
rect(cx - W * 0.09, H * 0.79, cx - W * 0.06, H * 0.88, RED); ellipse(cx - W * 0.075, H * 0.80, 2.5, 2.5, STEEL)      # multitool
ellipse(cx + W * 0.08, H * 0.82, 6, 8, STEEL); ellipse(cx + W * 0.08, H * 0.82, 3.5, 5.5, BELLY)                  # carabiner
line(cx + W * 0.08, H * 0.85, cx + W * 0.06, H * 0.91, 2.5, (220, 190, 60)); line(cx + W * 0.08, H * 0.85, cx + W * 0.10, H * 0.92, 2.5, BLUE)  # keys
# feet
ellipse(cx - W * 0.10, H * 0.97, W * 0.08, H * 0.035, FEET); ellipse(cx + W * 0.10, H * 0.97, W * 0.08, H * 0.035, FEET)

# --- downsample (box filter) + quantize to <= 224 colours (4-bit per channel steps, then cap)
out = []
for y in range(H):
    row = []
    for x in range(W):
        r = g = b = 0
        for yy in range(y * SS, y * SS + SS):
            for xx in range(x * SS, x * SS + SS):
                p = img[yy][xx]; r += p[0]; g += p[1]; b += p[2]
        n = SS * SS; row.append((r // n, g // n, b // n))
    out.append(row)
def q(c, step): return tuple(min(255, int(round(v / step)) * step) for v in c)
step = 16
while True:
    qo = [[q(p, step) for p in row] for row in out]
    colours = {p for row in qo for p in row}
    if len(colours) <= 224: break
    step += 8
with open(OUT, 'w') as f:
    f.write("P3\n# Surface Duo workbench logo (tools/linux/splash/make-penguin-logo.py)\n%d %d\n255\n" % (W, H))
    for row in qo: f.write(" ".join("%d %d %d" % p for p in row) + "\n")
if PNG:
    raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in qo)
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(PNG, 'wb').write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
                          chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
print("%s: %dx%d, %d colours (step %d)" % (OUT, W, H, len(colours), step))
