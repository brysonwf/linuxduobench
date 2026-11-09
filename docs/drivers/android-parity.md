# Workbench — Android parity (driver configuration tables)

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)).

**GOAL (the tester 2026-11-07: "make a goal to reach parity with each one of these drivers"):** every driver uses the
phone's OWN configuration tables (vendor partition / Microsoft DT / Android DT) to behave like Android, not just
"works". Source data stays on the phone or in `(local file, not published)` (proprietary, never committed); the repo holds the
parsers, the driver code and what we learned.

Rule of thumb that started this: the camera had six sensor modes in the phone's own camx table and we used two (one
16:9 crop) - the tester saw "16:9 vs Android's full screen and square". Look for the table first.

## Status

| # | Driver | Android's tables (where) | Ours today | Parity work | State |
|---|---|---|---|---|---|
| 1 | Camera sensor modes | camx sensormodule (vendor `lib64/camera/com.qti.sensormodule.sony_imx351.bin`): 6 modes | ALL 6 (4:3, 1:1, wide, 16:9, 1080p, 1080p HS) via set_fmt (2026-11-07, 5d12762); DT `rotation = <90>` (Android roll 90; verified upright in Snapshot, default DTB 5302f4e4) | preview framing: Snapshot streams 16:9 into the 3:4 screen, Android uses 4:3 (camera.md 2026-11-07) | ~DONE |
| 2 | Camera tuning | camx tuned files (`com.qti.tuned.*.bin`): LSC, CCM per CT, AWB/AE, NR, AF | **Android's CCMs + LSC meshes in use** (2026-11-07: CCM normal-light set 2550..6000 K; LSC 4 illuminants on libcamera master's soft-ISP LSC, see "Camera LSC + CCM: DONE" below); own contrast AF (local search, 1280x720 sharpness, 2026-11-07) | low-light CCM set by gain, NR, Android's AF tuning (camx AF tables) | IN PROGRESS |
| 3 | Charger / battery | Microsoft DT (dtbo dt31): fcc-max 3.7 A, fv-max 4.40 V, usb-icl 3.0 A, sw-jeita (0..10 C 2.5 A/4.25 V, 10..40 C 3.6 A/4.35 V, 40..45 C 2.5 A/4.25 V), step-chg (3.8 V 3.6 A, 4.3 V 2.5 A), thermal mitigation 3.7/1.85/0.74/0.185 A, auto-recharge 95 %, packs 803 + 2900 mAh | driver: writable FCC/FV (capped at those maxima, patches/qcom-smbx-fcc-fv-props.py); guard: Microsoft's sw-JEITA + step profile OPT-IN (/etc/duo/charge-profile = android); guard now really controls the charger (was the Type-C psy); PC-port ICL fix | ENABLED on pmOS 2026-11-07 09:14 (the tester: "your call"; gauges 25.0/27.5 C before): guard logged "android profile: float 4350000 uV / charge current 3600000 uA" (10..40 C band), temps flat over the next minute; on the PC port input stays capped at 500 mA, so the gain shows only on a wall charger. Clean kernel (2026-11-07): the caps come from the DT battery node (3.0 A = usb-icl, because qcom_smbx also uses it as the wall-charger input limit; 4.40 V) and hw-JEITA off from `qcom,sw-jeita-enable` (Microsoft's own property) - so the 10..40 C band's 3.6 A is clamped to 3.0 A. Next: thermal mitigation steps; recharge 95 %. CHARGE LIMIT (2026-11-07, opt-in): `echo 80 > /etc/duo/charge-limit` holds the battery at 80 % (charge current 0 = runs from USB), resumes 5 % below; tested 90 % at 99 %: paused, restored 1.95 A on removal | ENABLED |
| 4a | Audio mixer (2026-11-07) | vendor etc/mixer_paths_tavil.xml (local copy (local file, not published)) | SAME as ours: speaker-mono = SLIM RX0 -> RX INT7 -> COMP7 + SpkrLeft COMP/BOOST/VISENSE/DAC, RX7 Digital Volume 84 (0 dB), WSA PA gain 0 dB (6 dB only in anc-handset) | MSFT "speaker-protected" path = VI feedback SLIM_4_TX -> VI_FEED_TX -> SLIM0_RX_VI_FB_LCH_MUX into the ADSP's speaker-protection module; mainline q6afe has no SP/ACDB loading -> the real gap is DSP SP + ACDB EQ/MBDRC | mixer DONE; DSP side open |
| 4 | Audio calibration | ACDB (vendor `etc/acdbdata`): EQ, mic gains, echo cancel, WSA speaker protection (VI sense) | raw codec path, RX7 at 0 dB, VI sense off | speaker protection (WSA881x VI feedback), mic gains, UCM from ACDB values. **Seen by the tester 2026-11-07: "audio clip on boot does not sound right"** - the login chime (a soft high bell, -23 dBFS RMS) is nearly inaudible on the untuned speaker; a 440 Hz tone at the same level and the chime +12 dB both play fine (A/B with the tester), nothing is cut off (a 0.5 s start-delay changed nothing, removed). Fix = speaker tuning: a PipeWire filter-chain (high-pass + EQ + limiter/make-up gain) from a mic-loopback measurement, ideally from Microsoft's ACDB EQ/MBDRC values (generated locally) | NEXT |
| 5 | Display | panel DT (dtbo dt31 "dsi panel lg v2 amoled cmd ... dv"): bl_ctrl_dcs, bl-min-level 1, bl-max-level 0x3ff; NO HBM entries; the brightness curve lives in Android's framework overlays, not the kernel | panel driver: raw backlight 0..1023 via DCS set_display_brightness_large (= Microsoft's range) | the userspace curve (Phosh slider is linear in DCS steps); thermal backlight caps (thermal.md) | range MATCHES (2026-11-07); curve/caps open |
| 6 | Thermal | Android thermal-engine config (vendor `etc/thermal-engine.conf`): virtual skin sensor (skin-therm + msm-therm + battery), CPU/GPU set point 47.5 C, charging levels 46.5/49.5/53.5 C, backlight caps 49-52.5 C, shutdown 56 C | skin-therm + msm-therm zones (DT) with 47.5 C CPU+GPU throttling + 56 C shutdown; charger guard applies the charging levels (thermal.md) | backlight caps; modem PA mitigation (no mobile data yet); cold rules | DONE (skin limits, 2026-11-07) |
| 7 | Haptics | DA7280 waveform memory (DT `dlg,mem-array`, identical to Microsoft's: 6 snippets, 8 sequences) + Qualcomm's vibrator HAL (vendor `vendor.qti.hardware.vibrator`, InputFFDevice: FF_PERIODIC/FF_CUSTOM with custom_data[0] = the Android Effect number = the DA7280 PS_SEQ_ID) -> sequence 0 click, 1 double-click, 2 tick, 3 thud, 4 pop, 5 heavy-click, 6/7 extras | one constant buzz (FF_CONSTANT / the pending FF_RUMBLE patch) | tools/linux/haptics/duo-haptic-effect.py plays an effect by name the same way (2026-11-08, written, NOT played yet: with the tester holding the phone); then feedbackd/Phosh/Plasma event -> effect mapping | IN PROGRESS |
| 8 | Flash LED | phone's DT (dtbo dt31, qpnp-flash-led-v2 on PM8150L): flash_0 1200 mA / 190 ms, flash_1 85 mA (pre-flash), torch 135 mA, vph-droop detection, thermal derate 200/500/1000 mA | torch 135 mA (= Android; Phosh's flashlight), flash 500 mA, timeout up to 1.28 s | flash 1200 mA with a <= 190 ms pulse + 85 mA pre-flash - only once a camera path fires the flash (libcamera's simple pipeline has no flash control); check whether leds-qcom-flash derates like Microsoft before raising the current | torch MATCHES (2026-11-07); flash parked (unused) |
| 9 | Touch | digitizer feature reports, pen | iptsd defaults, pen untested | pen, palm rejection from the device's reports | TODO (pen hardware) |
| 10 | Light/proximity | SSC configs (used) | light + proximity in iio-sensor-proxy again (libssc 0.2.2 open_sync hang patched for all 5 sensors, 2026-11-07, sensors.md) | Android's auto-brightness curve (framework overlay, userspace) | DONE (driver) |
| 11 | Auto-brightness curve (2026-11-08) | the Duo's framework overlay vendor `overlay/FrameworksResSurfaceDuo-V1.apk` (read with tools/linux/display/android-res-dump.py): config_autoBrightnessLevels (18 lux steps 1..20000), config_autoBrightnessLcdBacklightValues (19 settings 11..255), brighten debounce 1.5 s, darken 8 s, setting default 82 / min 1 / max 255 | duo-autobrightness.py now uses exactly that curve (setting -> panel (v-1)/254 like Android's BrightnessSynchronizer; 0 lux = 3.9 %, 100 lux = 37 %, 1000 lux = 60 %) and the same debounce (offline test: brighten after 1.5 s, darken after 8 s, a 0.5 s flash ignored) | Android uses a monotone spline (we interpolate linearly between the same points); brightness slider default 82 | DONE (on pmOS at the next prepare; Debian needs libssc first) |
| 12 | Hinge geometry (2026-11-08) | overlays: `config_display_features = hinge-[1350,0,1434,1800]`, `config_mainBuiltInHingeMaskWidth = 84` | Debian/KWin: DSI-2 placed one hinge (33 logical px) right of DSI-1 (org.duo.HingeGap autostart; injected-tap grid confirms touch follows, edge x 1092.4). pmOS/phoc: still adjacent | pmOS: phoc.ini DSI-2 x 450 -> 478 once duo-edge.py / Hingeboard are checked against a gap | Debian DONE, pmOS TODO |

Already at parity: Wi-Fi board data (bdwlan), sensor DSP configs (SSC JSON, patched), modem MBN, GPS assistance
(engine lock + XTRA, 2026-11-07), camera sensor modes.

## Camera LSC plan (2026-11-07) - superseded: the series was already merged in libcamera master (next section)
Upstream already has it in review: "LSC for SoftISP simple pipeline" (Milan Zamazal, based on Hans de Goede's students'
work; libipa LSC grids per colour temperature uploaded to the GPU debayer as an RGB texture) - patchwork.libcamera.org
v15 3/6 = patch 28107 (Aug 2026). Plan: (1) apply the series on our 0.7.2 build (duo-libcamera-build-pmos.sh; external
code -> the tester runs), (2) extract Android's per-illuminant R/Gr/Gb/B meshes from com.qti.tuned.sony_imx351.bin
(chromatix LSC, next to the CCMs) and convert them to the series' tuning format (generated locally), (3) daylight
colourful-scene A/B against Android/BRIO.

## Camera LSC + CCM: DONE (2026-11-07 08:50)
- libcamera MASTER (0.8.0) already contains the softisp LSC series (merged 2026-10-22) -> `LSC=1
  duo-libcamera-build-pmos.sh` (fixes for master: IPA option/name `softisp`, softisp.cpp needs span.h, our AE patch
  skipped, PipeWire plugin: C++20 + properties::Location rename + `-Db_ndebug=true` (with asserts on, 0.8's
  ControlValue::get asserted on array defaults and crashed pipewire/wireplumber), FrameDurationLimits default scalar.
  Rollback: /var/tmp/libcamera-0.7-backup.tar.gz on the phone.
- Android's LSC: chromatix-lsc-extract.py (--yaml 17) - values must be QUANTIZED UQ2.6 (1.0 = 64; floats = 7x dark).
  Orientation on a real flat field (white paper on the lens, lamp): grid AS STORED is right (corners/centre 1.00,
  tint L-R 0.2) vs none 0.71; mirrored/flipped/rotated all worse. Tuning on the phone:
  /usr/share/libcamera/ipa/softisp/imx351.yaml = BlackLevel, Awb, Lsc (4 illuminants), Ccm (5), Adjust, Agc.
- **Found on the way - a black corner in most pictures:** libcamera picks the square mode (3) for 1920x1080 output, and
  Android's square table crops at offset 0,0 (array top-left; CamX recentres elsewhere) -> 25 % x 75 % of the frame
  outside the lens image circle. Driver now centres it (0x0408 = 936, 0x040a = 356) and mode 1 uses the full camx
  1080p delta (the hand subset missed the Y window 0x0347/0x034b and analogue regs). Flat field after: 0 % dark.

## Order (Claude's proposal, 2026-11-07)
camera tuning (visible every photo; files already on the phone) -> charger profile (faster + safer charging) ->
audio calibration (speaker loudness/protection) -> display -> thermal -> haptics -> flash -> touch/pen.
