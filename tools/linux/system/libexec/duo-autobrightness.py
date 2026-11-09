#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Surface Duo auto-brightness: the ambient light sensor (SLPI via iio-sensor-proxy) sets BOTH panels' backlights.
Phosh / gnome-settings-daemon drive only one backlight, the Duo has two (DSI-1, DSI-2).

Curve = ANDROID'S OWN (2026-11-08, android-parity.md): the Duo's framework overlay (vendor/overlay/
FrameworksResSurfaceDuo-V1.apk) config_autoBrightnessLevels / config_autoBrightnessLcdBacklightValues, interpolated in
lux like Android's mapping, setting 0..255 -> panel fraction (v - 1) / 254 like Android's BrightnessSynchronizer.
Debounce like Android's: brighter light must hold 1.5 s, darker light 8 s (config_autoBrightness*LightDebounce), and
only a clear change moves the screens (> 8 % from what this service set last), then a 2 s / 3 s fade.
MIRROR: Phosh's brightness slider writes one
backlight only - a change this service did not make is copied to the other screen, and it holds until the light
changes a lot (~3x brighter or darker than when the slider moved). /etc/duo/autobrightness-off switches the AUTOMATIC part off (the mirror keeps working).
Runs as root (OpenRC duo-autobrightness).
"""
import glob, math, os, time
import gi
from gi.repository import Gio, GLib

OFF = "/etc/duo/autobrightness-off"
# Android's curve (the Duo's framework overlay; numbers only - read with tools/linux/display/android-res-dump.py)
LUX = [0, 1, 2, 5, 10, 21, 40, 70, 110, 165, 250, 400, 650, 1000, 1700, 3000, 5000, 10000, 20000]
SETTING = [11, 26, 32, 44, 55, 67, 79, 89, 97, 106, 113, 125, 141, 154, 172, 194, 213, 236, 255]   # 0..255
TABLE = [(l, (s - 1) / 254.0) for l, s in zip(LUX, SETTING)]
DEBOUNCE_UP, DEBOUNCE_DOWN = 1.5, 8.0      # seconds (config_autoBrightnessBrightening/DarkeningLightDebounce)
state = {"set": None, "fade": None, "written": {}, "pending": None}

def curve(lux):
    lux = max(lux, 0.0)
    for (x0, v0), (x1, v1) in zip(TABLE, TABLE[1:]):
        if lux <= x1:
            return v0 + (v1 - v0) * (lux - x0) / (x1 - x0)
    return TABLE[-1][1]

def backlights():
    return sorted(glob.glob("/sys/class/backlight/*"))

# BALANCE (2026-11-08: one screen looked brighter than the other): the two AMOLED panels are not equally bright
# at the same DCS value (this phone: the left one, DSI-1 = ae94000.dsi.0, clearly brighter on camera). Per-panel factor,
# one line per backlight: /etc/duo/brightness-balance  "ae94000.dsi.0=0.8" (1.0 = as is; missing file = no balance).
BALANCE_FILE = "/etc/duo/brightness-balance"
def gain(b):
    try:
        for line in open(BALANCE_FILE):
            k, _, f = line.strip().partition("=")
            if k == os.path.basename(b) and f:
                return min(1.0, max(0.2, float(f)))
    except (OSError, ValueError):
        pass
    return 1.0

def write(level):
    for b in backlights():
        try:
            mx = int(open(b + "/max_brightness").read())
            v = max(1, round(level * gain(b) * mx))
            cur = int(open(b + "/brightness").read())
            if v != cur:                         # each write is a command to the panel: only real changes
                open(b + "/brightness", "w").write(str(v))
            state["written"][b] = v
        except OSError:
            pass

def mirror():
    """a backlight changed by someone else (the brightness slider): copy it to the other screen(s)"""
    for b in backlights():
        try:
            v = int(open(b + "/brightness").read()); mx = int(open(b + "/max_brightness").read())
        except (OSError, ValueError):
            continue
        last = state["written"].get(b)
        if last is not None and v != last:
            if state["fade"]:
                GLib.source_remove(state["fade"]); state["fade"] = None
            state["written"][b] = v              # the slider's screen: leave it alone (a second writer flickers)
            for o in backlights():
                if o != b:
                    try:
                        omx = int(open(o + "/max_brightness").read())
                        ov = max(1, min(omx, round(v / mx / gain(b) * gain(o) * omx)))
                        open(o + "/brightness", "w").write(str(ov)); state["written"][o] = ov
                    except OSError:
                        pass
            state["set"] = min(1.0, v / mx / gain(b))
            state["manual_ll"] = state.get("ll")   # automatic pauses until the light changes a lot
            print("slider %s -> %d %% on both screens" % (os.path.basename(b), round(100 * v / mx)), flush=True)
            break
        state["written"][b] = v
    return True

def fade_to(target):
    start = state["set"] if state["set"] is not None else target
    if state["fade"]:
        GLib.source_remove(state["fade"])
    if abs(target - start) < 0.005:         # nothing to fade (e.g. the first reading): one write at most
        write(target); state["set"] = target; state["fade"] = None
        return
    n = 6                                   # 6 steps (each a panel command): 2 s brighter, 3 s darker
    period = (2000 if target > start else 3000) // n
    steps = {"i": 0}
    def tick():
        steps["i"] += 1
        lv = start + (target - start) * steps["i"] / n
        write(lv); state["set"] = lv
        if steps["i"] >= n:
            state["fade"] = None; return False
        return True
    state["fade"] = GLib.timeout_add(period, tick)

def on_level(lux):
    if os.path.exists(OFF):
        return
    # smooth in log space (a hand passing, a flicker or a reflection should not move the screens), then act only on
    # a clear change of the target level
    ll = math.log10(max(lux, 0.0) + 1.0)
    state["ll"] = ll if state.get("ll") is None else state["ll"] + 0.35 * (ll - state["ll"])
    decide()

def decide():
    """the light only produces events when it changes: also called every 0.5 s so a debounce can complete"""
    if state.get("ll") is None or os.path.exists(OFF):
        return True
    lux = 10 ** state["ll"] - 1.0
    m = state.get("manual_ll")
    if m is not None:
        if abs(state["ll"] - m) < 0.5:            # the slider was used: keep it until the light is ~3x off
            return True
        state["manual_ll"] = None
    target = curve(lux)
    if state["set"] is None:                       # first reading: no debounce
        fade_to(target); return True
    if abs(target - state["set"]) <= 0.08:
        state["pending"] = None; return True
    # Android-style debounce: the new direction must hold 1.5 s (brighter) / 8 s (darker) before the screens move
    up = target > state["set"]; now = time.monotonic(); p = state["pending"]
    if p is None or p[0] != up:
        state["pending"] = (up, now); return True
    if now - p[1] < (DEBOUNCE_UP if up else DEBOUNCE_DOWN):
        return True
    state["pending"] = None
    print("light %.1f lux -> %d %% (Android curve)" % (lux, round(target * 100)), flush=True)
    fade_to(target)
    return True

bus = Gio.bus_get_sync(Gio.BusType.SYSTEM)
proxy = Gio.DBusProxy.new_sync(bus, 0, None, "net.hadess.SensorProxy", "/net/hadess/SensorProxy", "net.hadess.SensorProxy")
def claim():
    try:
        proxy.call_sync("ClaimLight", None, 0, 5000, None)
        v = proxy.get_cached_property("LightLevel")
        if v is not None:
            on_level(v.unpack())
        return False
    except GLib.Error as e:
        print("waiting for iio-sensor-proxy:", e.message, flush=True)
        return True
def changed(p, props, invalidated):
    d = props.unpack()
    if "LightLevel" in d:
        on_level(d["LightLevel"])
proxy.connect("g-properties-changed", changed)
GLib.timeout_add(300, mirror)
GLib.timeout_add(500, decide)
if claim():
    GLib.timeout_add_seconds(3, claim)
GLib.MainLoop().run()
