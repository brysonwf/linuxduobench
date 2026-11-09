# Workbench — Touch and pen

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)). Text moved unchanged from leg 33 on 2026-11-05; newest log entries first.

#### Touch (2026-11-02, kernels #17–#23)
- **Hardware** (Android dtbo #31, fragment@44/45 `d5_hid@0`): Microsoft HID-over-SPI digitizer "ts_d5" on QUP SE7
  SPI (`&spi7`, 0x89c000, 25 MHz), IRQ TLMM 3 level-high, reset TLMM 0, TLMM 1 "active" pull-up, vdd PM8150 L9.
  One chip serves both screens. HID ID **045E:0C01**.
- **Protocol:** the chip speaks Microsoft's pre-release **v0.9** HID-over-SPI (input header version 2). The upstream
  2026 spi-hid series (v1.0) rejects it ("Unknown input report version (v 0x2)"), so we ported Microsoft's own
  Duo 1 driver (`tools/linux/drivers/spi-hid-duo/`, from `microsoft/surface-duo-oss-kernel.msm-4..14`). 4.14→6.19
  breaks were small: `__assign_str` one-arg, `strlcpy`→`strscpy`, `spi->master`→`spi->controller`, void `remove`.
- **Why it was silent at first:** the UEFI leaves reset (gpio0) as an output driven high; Android's pinctrl states
  only change bias, so "reset assert" never pulled it low. The DT states now drive it (`output-low`/`output-high`).
  After that the chip answers at boot: reset → device descriptor → 920-byte report descriptor.
- **Report descriptor** (`(local file, not published)`, `tools/linux/hid-rdesc.py`): six
  applications. Two are **Capacitive Heat Map Digitizers** (page 0x0D usage 0x0F, one per screen) carrying
  IPTS-style heatmap reports (scan time 0x56 + gesture data 0x61, set-mode 0xC8, metadata 0x63) — the format
  linux-surface's **iptsd** decodes for multitouch + pen. One vendor application (0xFFA1/0x60) holds **finished
  single-touch reports, ID 64 and ID 66** (one per screen: tip + X/Y 0..32767).
- **Our driver tweaks:** rewrite that vendor application header to Digitizer/Touch Screen (`05 0d 09 04`) so
  hid-input maps reports 64/66, and set `HID_QUIRK_MULTI_INPUT` in `->start()` (hid-core resets quirks in
  `hid_add_device()` and again at probe) → one input device per screen. Verified with evtest: BTN_TOUCH + ABS_X/Y
  stream while dragging (2,381 events in 20 s; 475 reports from one screen, 349 from the other).
- **Screens:** report 64 → first touch input (event256) = **right** screen; report 66 → event257 = **left** screen
  (left-only test: 641 events on event257, 0 on event256). Axes 0..32767 each.
- **Next:** multitouch + pen via iptsd on hidraw0 (`tools/linux/build-iptsd.sh`,
  the tester runs it — building external code is blocked for Claude); hide the 33 vendor-report input nodes.


#### Multitouch via iptsd (2026-11-03, kernel #30)
- Port fixes in `spi-hid-duo`: GET_REPORT returns the byte count (4.14 returned 0 → empty feature reports) and,
  with `spi_hid_duo.getreport_id=1`, puts the report ID first (hidraw layout). Feature reports 6/14 (metadata, one per
  screen): sensor 27×21, 8855×11437 (0.01 mm = 88.6×114.4 mm).
- Warm-reset fix: gpio1 is the digitizer's power-enable/wake line; the pinctrl states now DRIVE it (UEFI leaves it an
  output high, so after PS_HOLD/watchdog resets the chip saw no power-on edge and stayed silent). 100 ms hold.
- **iptsd** (linux-surface, static aarch64 build via `tools/linux/build-iptsd.sh`): `iptsd-check-device` → "IPTS
  device", touchscreen mode. The daemon creates "IPTSD Virtual Touchscreen/Stylus". Two-finger test: 834 MT position
  events in 25 s, slots 0–6, 590 heatmap IRQs → **multitouch works**. Default config (no `045E:0C01` preset) gives
  contact flicker (217 new tracking IDs) — next: a Duo preset (size, invert/rotation per screen), autostart from init.
- **Per-screen report IDs** (raw hidraw captures while touching one screen, `tools/linux/hid-report-ids.py`):
  right screen = heatmap report **13** (1st Capacitive Heat Map collection, IDs 5–28) + single-touch 64;
  left screen = heatmap report **63** (2nd collection, IDs 14/57–78) + single-touch 66. Both heatmaps 1499 B.
  One iptsd on one hidraw mixes both screens → the contact flicker. Fix in progress: the driver registers one HID
  device per screen (each gets its own hidraw + iptsd).
- **Why iptsd flickered (2026-11-03, kernels #31–#33):** each heatmap report (13 right, 63 left) carries an IPTS
  HID frame with the heatmap (21×27 = 567 B, baseline ~180, touch ~80–95) plus a reports frame (type 0xFF) with a
  **dimensions report saying rows 21, cols 27, z 0..1**. iptsd normalises `(v − z_min)/(z_max − z_min)` → with
  z_max = 1 every cell is garbage; and it maps the buffer column-major with rows as the fast axis, while the data's
  fast axis has 27 cells (a held finger is one blob only at stride 27). Driver quirks `fix_zmax` (z_max ≤ 1 → 255)
  and `swap_dimrep` (rows↔cols, x↔y ranges) patch the report in place (`spi_hid_fix_heatmap`). Also: one HID device
  per screen (`spi 045E:0C01` right + `… left`, own hidraw each → one iptsd each). Decoders: `ipts-frames.py`,
  `hid-report-ids.py`; A/B script `iptsd/abtest.sh`. Verification touch test pending (Morning list).
- [ ] Touch check on kernel #33 (spoken cue): one finger held on the left screen 10 s → `sh /tmp/ab.sh 0 8.86 11.44 10`; expect new contacts ≈ 1–3, not ~200.


## Two HID-over-SPI stacks - plan (2026-11-07, kernel-clean item "spi-hid-duo next to the upstream spi-hid series")
Both bind `compatible = "hid-over-spi"`; the Duo DT uses ours (CONFIG_SPI_HID_DUO, Microsoft's v0.9 port).
What the Duo needs that the upstream series (v1.0 protocol, `spi-hid-of.c`: header/body/output addresses + opcodes from
properties) does not do today:
1. Power/reset through THREE pinctrl states (active / sleep / reset) that DRIVE gpio0 (reset) and gpio1 (power-enable):
   the UEFI leaves both as outputs, bias alone never resets the chip; after a warm reset gpio1 must be held low ~100 ms.
2. One SPI device = TWO HID devices (right screen 045E:0C01, left screen given 0C02 via `left_pid`) so compositors can
   map each screen.
3. Chip quirks: heatmap dimension report z_max <= 1 -> 255 (`fix_zmax`), GET_REPORT reply layout (byte count +
   report ID).
Cleanup due in OUR driver (no functional change; do it in a session where touch can be tested by hand): drop the
test-only module parameters `getreport_id` ("test switch"), `swap_dims` and `swap_dimrep` (experiments, default off;
the chip's 21x27 is right) and make `fix_zmax` / `left_pid` fixed behaviour - the clean-kernel rule (no knobs).
Long term: port 1-3 as quirks onto the upstream spi-hid core (Microsoft-HID-over-SPI v1.0 vs the chip's v0.9 protocol
is the first thing to check) and drop the second stack.

## Log

- 2026-11-02 late: touch get-feature fix (kernel #25 returns data; #26 adds the report ID) → #26 hung twice (boot / first hidfeat). Loop stopped. Phone left at fastboot (core). Next: TWRP → put #25 back + GRUB entry with `initcall_blacklist=msm_drm_register`, then find the #26 hang.
- 2026-11-03 07:20: touch survives warm resets (gpio1 driven); get-feature report IDs; **iptsd multitouch works** (2 fingers, 7 slots).
- 2026-11-03 07:40: touch flicker root cause = chip dimension report z_max=1 + rows/cols order; quirks in kernel #33; one HID device per screen.
- 2026-11-04 14:30: **TOUCH -> WRONG SCREEN fixed** (the tester: "tapping the top screen touches the keyboard"). Ground truth by counting evdev bytes while the tester tapped the top screen: all on event258 = iptsd of hidraw0 = spi-hid child .0001 -> hidraw0 is the camera-side WORK screen (DSI-2); default DUO_TOUCH_OUTPUTS is now "DSI-2 DSI-1" (both digitizers have identical HID descriptors, creation order is the only key). Second cause: `udevadm info` showed WL_OUTPUT=DSI-1 on BOTH nodes - a stale udev db entry (event numbers repeat across desktop restarts; reload+trigger did not refresh it) -> duo-desktop.sh now checks every node's WL_OUTPUT before sway starts and re-runs `udevadm test` on a stale one (caught one on the next run).
- 2026-11-04 11:45: DESKTOP GOAL set by the tester: "one screen should be the keyboard and the other screen should work like a track pad and allow zoom like the old VNC phone app would do" → keyboard screen + VNC-style touchpad screen (relative pointer, tap = click, pinch zoom/pan of the view).
- 2026-11-04 09:30: pen rig ready (`tools/linux/iptsd/pencheck.sh [s]`: iptsd creates 2× "IPTSD Virtual Stylus"; records evtest + raw hidraw); test PARKED — the tester's pen battery is dead (Morning list: charge it, then run pencheck.sh 60).
- 2026-11-04 08:45: **touch tracking FIXED**: our swap_dimrep quirk was wrong (chip's 21x27 is right; stride 27 = one blob per frame) → default off (kernel #50); live: hold 1 contact, drag 1 contact / 221 positions.
- 2026-11-04 08:35: touch: dims report says width 21 while the heatmap stride is 27 (our swap_dimrep); dims-swapped replay mixed (hold better, drag worse) → touch tracking parked with 4 next ideas.
- 2026-11-04 08:00: touch raw heatmaps captured (hold + drag): clean signal, zero dropouts → touch churn is in iptsd's tracker; build-hexagonrpcd.sh now loads the dev overlay itself.

## Touch fix notes (moved from the Morning list, 2026-11-04 07:58-08:40)

Touch re-check (2026-11-04 07:58, `tools/linux/iptsd/touchcheck.sh 60`, quirks fix_zmax + swap_dimrep on, kernel #48):
  BOTH screens register, multi-touch works (2 and 3 fingers at once), X spans the full 0..9600. But contacts churn:
  screen A 15 contacts / 120 positions, screen B 143 contacts / 182 positions (a drag should be ONE contact) →
  iptsd contact-tracking tuning needed (`[Contacts]` options in the preset) — more finger tests later. Pen untested.
  **Raw heatmaps (08:00, `cat /dev/hidrawN` while iptsd runs; `(local file, not published)`,
  `tools/linux/iptsd/heatmap-stats.py`):** 21×27 heatmap, baseline 180, a finger pushes ~8 cells 77–100 below it
  (iptsd threshold 24); hidraw0 = a smooth drag (row ≈10, col 1→20), hidraw1 = a steady hold (row 12, col 14) for
  30 s; ZERO dropouts in 1211 frames (~18 Hz per screen). The sensor data is clean → the churn comes from iptsd's
  tracker/config, not the hardware or our driver. Next (offline): replay these captures through iptsd (or port its
  contact finder settings) and compare contact IDs; candidates: PositionThresholdMax vs 18 Hz frame rate, Size*.
  **Offline replay rig (08:20, kernel #49 = CONFIG_UHID):** `tools/linux/uhid-replay` (virtual HID device: recorded
  descriptor — clamped with `iptsd/hid-report-sizes.py`, uhid allows ≤ 4 KiB reports and the Duo declares 7 KiB ones —
  answers iptsd's GET_REPORT 6 with the recorded metadata and its HIDIOCSFEATURE mode switch, plays the raw reports)
  + `tools/linux/iptsd/replay-test.sh <dir> <hidraw0|1> <preset>` → the REAL iptsd on the replayed data, no finger.
  Reproduces the churn: hold = 44 contacts / 78 positions, drag = 19 / 317. iptsd lifts the held contact (slot 2,
  tracking ID 2) and re-adds it on the next frame, 45 times; a short second contact appears on slot 0. Constant 44
  for every [Contacts] variant tried (stability thresholds ×2.5, activation 40/30, size 0.05–5 cm, aspect ≤ 10) →
  structural, not tuning. Next: read iptsd's contact validity/lift logic (daemon touch handling, finder) offline.
  **iptsd source + report decode (08:35):** iptsd lifts a contact when it is missing from a frame, invalid (temporal/
  size/aspect) or outside the overshoot margin. Each Duo report = HID frame 0x00 { 0x01 heatmap 567 B, 0xFF container
  of IPTS reports: 0x00 timestamp, **0x03 dimensions `1b 15 00 1a 00 14 00 ff` (height 27, width 21, z 0–255, constant —
  as patched by our swap_dimrep quirk)**, 0x04 (52 B, 94 distinct — likely the chip's own contact list), 0x06, 0x07,
  0x32 }. The heatmap's fast axis is 27 cells, so a dims report saying width 21 makes iptsd slice rows at stride 21.
  Replay with the dims swapped back (height 21, width 27): hold lifts 14 → 1, but drag lifts 3 → 12 — inconclusive;
  my evtest counters are too crude. (Was parked briefly; fixed below.) Ideas tried: (1) a proper evtest parser (per-slot
  sessions); (2) render the heatmap at stride 21 vs 27 (`heatmap-stats.py`) to see which keeps the blob compact in
  BOTH captures; (3) check how linux-surface configures Gen7 devices with a 0x03 dims report; (4) the chip's own
  0x04 contact list could feed hid-multitouch directly, bypassing iptsd.
  **✅ FIXED (08:40, kernel #50):** idea (2) settled it — rendering the recorded heatmaps at stride 27 gives exactly ONE
  blob in every touched frame of both captures (538/538, 659/659); at stride 21 the finger falls into 2–4 pieces in
  nearly every frame. The chip's own dims report (21 rows × 27 cols) is RIGHT; our `swap_dimrep` quirk made it 27 × 21
  → iptsd sliced at stride 21 → shattered, flickering contacts. `swap_dimrep` now defaults OFF (kernel #50; it is a
  0644 parameter, toggled live first). Live test: **hold = 1 contact, drag = 1 contact with 221 smooth positions**
  (before: 15 / 143 contacts). fix_zmax stays on. Pen still untested.
