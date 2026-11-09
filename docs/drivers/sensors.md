# Workbench — Sensors (SLPI) and CDSP

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)). Text moved unchanged from leg 33 on 2026-11-05; newest log entries first.

## Current understanding (2026-11-05)
- **Sensors work and are DEFAULT-ON** since initramfs #90 (off: `duo.sensors=0`; before that they needed
  `duo.sensors=1` - the sections below keep that older wording as history). SLPI → FastRPC → hexagonrpcd (our patched
  build, sda10 /opt/bin) → libssc: accel, mag, gyro, hinge_angle, posture, hall, ambient light, proximity.
  Read: `ssccli --sensor accelerometer`, `ssc-dump hinge_angle` (95-98 deg = opened like a laptop).
- **Keys:** DT remote-heap pool + vmids on the SDSP FastRPC node (without it the attach froze the SoC), vendor_a from
  `super` at sector 11369472, hexagonrpcd -R root layout + socinfo, light/proximity configs with the Android-property
  condition replaced (RAM copy).
- **CDSP:** boots with `duo.cdsp=1`; no client yet. hexagonrpcd only does INIT_ATTACH (root PD), so a user-PD test needs
  quic/fastrpc's libcdsprpc (not packaged in Alpine; building it = the tester) + remote_handle64_open of the phone's own v66
  libbenchmark_skel.so (quic/fastrpc's own test skels are v68/v75 = too new for SM8150's v66). Parked until something
  needs the CDSP.

#### Sensors + hinge (2026-11-03, offline prep, kernel #38)
- The Duo's sensors — including the **hinge angle** — sit behind the **SLPI** sensor DSP (Qualcomm SSC); the Android
  DT has no hinge/Hall/accelerometer I²C nodes. Public stack (works on the SM8150 Xiaomi Mi 9 "cepheus"): SLPI fw →
  FastRPC (`/dev/fastrpc-sdsp`) → **hexagonrpcd** (serves the sensor config/registry from vendor + persist, read-only)
  → **libssc** (`ssccli`) → iio-sensor-proxy. Alpine 3.22 community has hexagonrpcd 0.3.2, libssc 0.2.2,
  iio-sensor-proxy 3.6 (added: hexagonrpcd + libssc).
- Kernel: mainline already has the SDSP FastRPC node (3 compute banks); added the Oct-2026 v2 fix "fastrpc: Allocate
  message buffer from remote heap for sensors PD" (`patches/fastrpc/`; SMMU faults on SM8150 without it).
- DT: `remoteproc_slpi` → `slpi.mdt` (split firmware from modem_a). Init: `duo.sensors=1` starts SLPI (recovery off),
  mounts vendor_a + persist read-only under /run/hexagon and runs `hexagonrpcd -f /dev/fastrpc-sdsp -d sdsp -s -R`.
  Known elsewhere: SLPI may crash until undeclared sensor power rails are described (sm-x800-linux notes). Hinge is
  likely a Microsoft-specific SSC sensor type — check `ssccli` output for it. Untested.


#### Sensors WORK (2026-11-03 night, kernel #41 + DTB #44 + initramfs #50)
`duo.sensors=1` → SLPI up, hexagonrpcd attached, **accelerometer and magnetometer stream data** via `ssccli`
(accel Y ≈ −10.0 m/s² = gravity with the phone upright). Four fixes, found with `tools/linux/duo-sensors-try.sh`
(step-by-step, with `/dev/kmsg` streamed to the PC so a freeze leaves its last lines):
1. **FastRPC remote heap** — the sensors-PD fix sends message buffers to a reserved pool that sm8150.dtsi doesn't
   declare ("no reserved DMA memory for FASTRPC"); the first INIT_ATTACH_SNS then **froze the whole SoC** (bus stall —
   the watchdog could not reset it; the tester's power button twice). Fix: `fastrpc_mem` shared-dma-pool (16 MiB,
   reusable) + `qcom,vmids = HLOS MSS_MSA SSC_Q6 ADSP_Q6` on the SDSP fastrpc node, copied from sdm845.dtsi.
2. **vendor_a lives in `super`** (no GPT entry): Droidian's dm table gives one linear extent at super sector 11369472
   → read-only loop at that offset. `dsp_a` (sde7) mounted on vendor/dsp, persist read-only.
3. **hexagonrpcd's `-R` root has its own layout** (linux-msm/hexagonrpc `rpcd_builder.c`): `dsp/sdsp/`,
   `sensors/config/`, `sensors/sns_reg.conf`, `sensors/registry/` (written to → tmpfs copy), `socinfo/`.
4. **socinfo files are mandatory** on this firmware (hexagonrpc issue #23: same "Could not find local interface
   sns_registry / Unsupported handle 4294967295"). The Duo's configs (`surfaceduo_*.json`) match `hw_platform`
   "Surface" + `soc_id` 339; `platform_subtype(_id)`/`platform_version` are placeholders (0) until read from Droidian.
Also: hexagonrpcd opened too early fails with EBUSY → init retries.
Still open: light + proximity return no samples (the `surfaceduo_txc_*` configs key on `platform_subtype_id` 11–18 —
read the real value from Droidian `/sys/devices/soc0`); **hinge angle** = two accelerometers + two magnetometers + hall
sensors (`_0`/`_1` configs, one per half) — derive it, or find Microsoft's SSC hinge sensor; the registry rewrite
(`temp.json` writes refused by hexagonrpcd's mapping) is harmless so far; wire `iio-sensor-proxy`/`ssc-server` later.
- **Real socinfo** (Droidian `/sys/devices/soc0`, 2026-11-03 22:06): hw_platform `Surface`, platform_subtype `MP_A`,
  platform_subtype_id `16`, platform_version `65536`, soc_id 339, revision 2.2 → in init (initramfs #51).
- **The Duo's SSC sensor list** (persist `sensors/sensors_list.txt`, written by Android): pedometer, sar_posture,
  proximity, hall, **posture**, tilt, gravity, mag, sig_motion, psmd, mag_cal, sensor_temperature, rotv, lightfusion,
  virt_device_orient, game_rv, **hinge_angle**, accel, gyro_cal, gyro, ambient_light, geomag_rv, resampler.
  → The hinge angle is a ready SSC sensor (no fusion needed). libssc/ssccli support only 5 types, so the next step is
  our own small client: libssc's `SSCClient` emits every raw "report" — open data type `hinge_angle` and decode the
  float array (`sns_std_sensor_event`, field 1).
- **HINGE ANGLE WORKS** (2026-11-03 22:17): `ssc-dump hinge_angle` (`tools/linux/ssc-dump/ssc-dump.c`, our generic
  SSC client: libssc's base SSCSensor + raw report decode, built natively on the phone with the dev overlay) →
  sensor "Hinge Angle", vendor Microsoft, one report per 100 ms while moving: `<angle> 0 <second estimate>`.
  the tester folded it live: 180° flat → 0° closed → 180° → ~352° (screens back to back) → back
  (`(local file, not published)`). Also streaming: **posture** (Microsoft; `5 2 32 0 -9.806 190`
  = posture codes + gravity + angle) and **hall** (ROHM "Surface Hall"). Binaries on sda10 `/opt/bin`, linked into
  /usr/bin by init on sensors boots.
- hexagonrpcd exits after the DSP's `sns_registry` miss (handle -1 → its listener loop treats the error as fatal;
  happens with the real platform_subtype_id 16). Sensors keep streaming without it, but the DSP loses file access.
  Fix: `tools/linux/patches/hexagonrpcd-keep-serving.py` (send the AEE error back, keep listening) +
  `tools/linux/build-hexagonrpcd.sh` (fetch v0.3.2, patch, build on the phone → sda10 `/opt/bin/hexagonrpcd`, which
  init prefers). Building fetched outside code is blocked for Claude → the tester runs it (Morning list).
- **✅ Light + proximity (2026-11-04 08:25, initramfs #56):** TXC **PA22A** ambient-light (45–46 lux) and proximity
  (config event; on-change). The `surfaceduo_txc_*` configs also require the Android property
  `ro.build.version.release`; init now gives hexagonrpcd a RAM copy of the config dir with that condition replaced by an
  always-true one (`soc_id` 339) and the `_a12` duplicates dropped. With the patched hexagonrpcd (the tester ran
  `build-hexagonrpcd.sh`; sda10 `/opt/bin/hexagonrpcd`) the daemon now stays up and the `sns_registry` failure is gone.
  All sensors on a `duo.sensors=1` boot: accel (STMicro LSM6DSO), mag, gyro, hinge_angle, posture, hall, ambient_light,
  proximity. (Earlier note, kept for history:)
- Light/proximity still silent with the real socinfo: their configs (`surfaceduo_txc_*`) also require the Android
  property `ro.build.version.release` 11/12, which hexagonrpcd cannot answer (no `sns_registry`/property service).


## pmOS: light sensor silent + iio-sensor-proxy hang (2026-11-07 00:20-00:30) - SOLVED 13:00
- **SOLVED (2026-11-07 13:00): light + proximity in iio-sensor-proxy, no freeze.** The light-only fix below was not
  enough, found step by step with gdb: (1) light fixed -> main thread still in a futex wait with 3 report-receiver
  threads (proximity's never started: proximity has the identical first-measurement wait); (2) light + proximity
  fixed -> `g_cond_wait <- ssc_sensor_compass_open_sync` (4 receivers up). All five libssc 0.2.2 sensors share the
  pattern, so `tools/linux/sensors/libssc-0.2.2-open-sync-hang.py` patches all five. Result on pmOS: monitor-sensor
  logged 65 light readings in 20 s (24-36 lux), proximity "far", the proxy still answers D-Bus (orientation, LightLevel).
  prepare puts ssc-light + ssc-proximity into 81-duo-ssc.rules only while the patched library is installed (marker
  /etc/duo/libssc-patched.md5 = its md5; an apk upgrade back to the stock 0.2.2 -> accel + compass only). Build also
  needed python3-dev (libssc's mocking/meson.build always runs). Upstream: report to libssc/pmOS = draft for the tester.
- Accelerometer: correct after `ACCEL_MOUNT_MATRIX` on `fastrpc-*` (iio-sensor-proxy opens the SSC through
  fastrpc-ADSP); auto-rotate verified.
- Ambient light (TXC PA22A): `ssc-dump ambient_light` finds the sensor and opens the stream, gets ONE config message
  (msg 768) and then nothing while the tester switched a lamp off/on and covered the sensor (2 runs, also with
  iio-sensor-proxy stopped). iio-sensor-proxy LightLevel stays 0.0. The RAM config patch (soc_id instead of the
  Android property) IS applied on pmOS (same as the workbench, where ALS read 45-46 lux).
- hexagonrpcd log: ~16,700 boot-time "Tried to open /mnt/vendor/persist/sensors/registry/temp.json for writing" + reads
  of .../registry/file1 - the DSP's registry writes go to the Android absolute path (not under -R). A symlink
  /mnt/vendor/persist/sensors/registry -> the RAM registry made no difference after boot (the burst is over); untested
  from boot. The workbench had no such path either, so probably not the light cause.
- After several proxy stop/starts + light experiments, iio-sensor-proxy answers Introspect but every Properties.Get
  hangs (main thread futex wait) - even fresh, with no client claim -> auto-rotate state unreadable; reboot needed.
  Gotcha (again): `iio-sensor-proxy -v` ignores `timeout` - never run it inside an ssh command that must finish.
- **ROOT CAUSE of the hang (00:33, fresh boot):** once anything claims the light sensor (Phosh does at login; our
  autofocus daemon did at 00:03), iio-sensor-proxy's MAIN LOOP stops: libssc's thread still logs accel "Message 1025"
  every 40 ms, but no "Accel sent by driver" line follows and every D-Bus call (Properties.Get, orientation for
  auto-rotate) times out; the 23:20 run without a light claim was fine. Workaround in 81-duo-ssc.rules (prepare):
  IIO_SENSOR_PROXY_TYPE = "ssc-accel ssc-compass" (light + proximity = the same silent TXC PA22A, left out) ->
  orientation answers instantly ('right-up'). Put light/proximity back once the PA22A reports on pmOS.
- **A/B on the RAM workbench (00:37, same kernel #86): light ALSO silent there** (msg 768 config only, lamp switched
  off/on twice), while **proximity works** on both (msg 769 on open: f1=0 far, f2=22 raw). Same chip -> powered and
  alive; the ALS half stopped reporting sometime after 2026-11-04 08:25 (45-46 lux then) on every system. Suspects:
  the registry/calibration path (DSP registry writes to /mnt/vendor/persist/... fail - 2448 lines on the workbench
  too), a config change since 11-04 (socinfo platform_subtype_id 0 -> 16?), or the ALS needing a lux change bigger
  than a lamp toggle at that spot. The init's sensor setup is byte-identical since #56 (git diff edff8bc..HEAD).
  CORRECTION: earlier the same night (23:13-23:14, pmOS) iio-sensor-proxy DID report LightLevel 1.0 then 5.0 lux, with
  auto-rotate working at the same time -> the ALS reports (on-change, a dark room = rare events) and the freeze came
  later. Open: what triggers the proxy main-loop freeze (2nd light claim? claim/release cycles?) - reproduce with
  light re-enabled and a -v log detached (setsid nohup), then report upstream (iio-sensor-proxy libssc driver).
- **ROOT CAUSE PROVEN (01:00, gdb on a detached test proxy with light re-enabled):** main thread =
  `g_cond_wait <- ssc_sensor_light_open_sync (libssc.so.0) <- ... <- g_main_loop_run`. libssc 0.2.2 (pmOS v25.06):
  open_sync waits for `light_thread_running`, which is set ONLY in the measurement handler; the PA22A is on-change, so
  in a steady room no measurement comes -> blocked forever (Phosh claims light at login = every boot). libssc 0.4.x
  restructured this (not affected). Fix: `tools/linux/sensors/libssc-0.2.2-light-open-hang.py` (idle callback on the
  report thread's loop declares it running; dry-run applied + idempotent) built by
  `tools/linux/sensors/duo-libssc-build-pmos.sh` (the tester runs: external code), then light/proximity go back into
  81-duo-ssc.rules. Upstream report to libssc/pmOS = draft for the tester first.
- Next: fresh boot, check LightLevel before anything claims; ssc-dump light vs proximity; diff the SSC registry/config
  traffic against a workbench boot (where ALS worked).

## Log
- 2026-11-08 15:55: **Debian: light sensor read 0 lux on every boot - fixed (duo-sensors waits for the sensors).**
  `ssccli --sensor light` (libssc 0.4.4) read 16-20 lux and iio-sensor-proxy run by hand reported "Light changed",
  but the boot instance logged `g_ptr_array_add: assertion 'rarray' failed` + `driver_set_polling: assertion
  'sensor_device' failed` and stayed at 0.0: it started (ordered after duo-sensors) while the SLPI's sensor service
  was still registering its sensors. duo-sensors now returns only once `ssccli --sensor light` answers (<= ~40 s;
  pmOS without ssccli: 5 s). Clean boot: 0 assertions, LightLevel 17 lux at once (auto-brightness had been running
  on 0 lux). duo-health: libssc 0.4+ = PASS (no open_sync patch needed).

- 2026-11-05 10:30: **Sensors default-on** (initramfs #90; off: duo.sensors=0). Re-verified on kernel #79 first with a one-shot flag: SLPI running, hexagonrpcd up on the first try, hinge_angle 95-98 deg (phone open like a laptop), accelerometer Z 10.05 m/s^2; then a one-shot of #90 and the plain default boot - same, camera + display unaffected.
- 2026-11-03 10:40: offline — sensors/hinge stack (SLPI + FastRPC fix + hexagonrpcd + libssc) prepared, kernel #38 + initramfs-38 built.
- 2026-11-03 12:50: offline — CDSP prepared (cdsp.mdt, duo.cdsp=1); DTB/initramfs #39; 5 pm plan written.
- 2026-11-04 12:00: LAPTOP POSTURE VERIFIED (BRIO): hinge horizontal, landscape wvkbd keyboard upright on the bottom (no-camera) screen, foot terminal on the top (camera) screen; both outputs transform 90. Per-panel touch via WL_OUTPUT udev rules (hidraw0→DSI-1, hidraw1→DSI-2; touch test by the tester pending). Note: restarting wvkbd over ssh needs WAYLAND_DISPLAY=wayland-1 + XDG_RUNTIME_DIR.
- 2026-11-04 08:30: **light + proximity work** (TXC PA22A; config property condition patched in a RAM copy) + patched hexagonrpcd stays up (the tester built it); initramfs #56.
- 2026-11-03 22:20: **hinge angle works** (`ssc-dump hinge_angle`, Microsoft SSC sensor; the tester folded it 0–352°) + posture + hall; dev overlay = compiler on the phone; hexagonrpcd keep-serving patch ready (the tester builds).
- 2026-11-03 21:30: CDSP boots (`duo.cdsp=1`, fastrpc-cdsp up, stays running). Windows check: stock UEFI refuses our GRUB at `\EFI\Boot\bootaa64.efi` → decision on Morning list; grubenv reset from TWRP.
- 2026-11-03 21:20: **sensors work** (accel + mag via ssccli): FastRPC remote-heap pool (fixed a SoC freeze), vendor from super, hexagonrpcd root layout + socinfo. Display: Droidian boot-time DSI dyndbg captured — downstream re-sends the PPS; tested (`pps2`), still noise.
