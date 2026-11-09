# Workbench — Display panels (DSI / DSC)

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)). Text moved unchanged from leg 33 on 2026-11-05; newest log entries first.

## Current understanding (2026-11-05)
- **Works:** both panels, 60 fps. Recipe = `msm.dsi_cmd_burst=0` + 2 DSC slices per DSI packet + non-continuous clock +
  300 ms settle before the first panel init + dispcc CLK_OPS_PARENT_ENABLE + **panel `hbp=552` (~700 Mbit/s DSI link,
  Android's rate; kernel #76)**. The old hbp=20 (~346 Mbit/s) made every frame miss a TE (30 fps) and caused the
  boot-time HS-lane FIFO underflows.
- **Open:** the camera-side panel's (R2, DSI1) FIRST init fails silently on most boots; a fb0 blank/unblank re-init
  always fixes it, so init #87 runs one unconditionally before the splash.
- **Ruled out for that fault** (log entries below): init order `seq=0`, DPU max bandwidth, the brightness write in panel
  enable, the splash, the venus/usbc DTB, `reinit=1`, L17A at 3.0 V, `firstdelay=1000` (worse). Early theories in the
  log (PPS sent in HS mode, PPS truncation) were disproved later.

## Log
- 2026-11-08 15:45: **pixel-clock warning fixed (kernel 0088, default 6.19.0-00088-g80ad70be4ebe).** Every boot warned
  twice `disp_cc_mdss_pclk{0,1}_clk_src: rcg didn't update its configuration` (WARN at clk-rcg2.c:136 from
  clk_pixel_set_rate during display probe): the RCG was reprogrammed while its parent, the DSI PHY PLL, was off.
  `CLK_OPS_PARENT_ENABLE` on both pixel sources (as qcm2290/sm6115/sm6350/sm6375/sm7150/sm8750 dispcc do) -> 0
  warnings on 4 boots (2 one-shot, workbench, Debian), both panels lit (webcam). Not a fix for the camera-side
  first-init failure: dsi_err_worker still fires on most boots (1, 0, 1). ESP vmlinuz.efi 7622c0da, initramfs
  79df4552 (.prev 3fe287d7 / ba2c8b38).

- 2026-11-08 00:00-01:20: **Left panel (DSI0) flickers/garbles while the brightness changes - investigated, not fixed.**
  Each brightness change is a DCS command; Phosh's slider sends dozens per second. Facts, all on DSI0 only:
  - The error is `dsi_err_worker: status=4` = DSI_ERR_STATE_FIFO (not a PHY error); a test print of FIFO_STATUS
    (from the err worker - printing it from the IRQ handler FROZE the phone via fbcon) reads `0xaaaa1310` /
    `0x88881010` = **HS FIFO underflow on all four lanes** (+ "full"), command-DMA watermarks set.
  - Stress test (`dsi-stress` scripts, a local folder; full-screen fbdev frames + 200 brightness writes): frames only = **0**,
    brightness only (cursor blink off, no frames) = **0**, both = **1-10** errors per 200 (high variance). So only a
    command overlapping frame traffic breaks it.
  - Android's final DSI0 register values (droidian-dsi-dyndbg.txt) vs ours: same DSC packing, streams, TRIG_CTRL
    (BLOCK_DMA_WITHIN_FRAME on) and MDP_CTRL2 (**burst OFF on Android too**, 0x6). Different: MDP_IDLE_CTRL 0x1001
    (DT `qcom,mdss-dsi-idle_ctrl`), HS_TIMER_CTRL 0x3fd08, EOT_PACKET_CTRL 0, DMA_FIFO_CTRL 0x110000. Set live by
    devmem, each alone and all together: **no change** (1-6 per 200 each).
  - Tried and NOT fixing it: brightness in LP mode; coalescing brightness writes (<= 1 per 60 ms, pending patch
    0002); waiting for CMD_MODE_MDP_BUSY before a command (duo/backup-dsiwait); a frame/command link claim between
    the DPU cmd encoder (prepare_for_kickoff .. pp_done) and DSI commands (duo/backup-dsilink: 6/3/0, plus write
    timeouts); claim + MDP-busy wait together (0/1/2 - inside the noise). Burst mode ON (panel without NO_CMD_BURST):
    0 errors but a garbled picture (DSC packing needs burst off here, as on Android).
  - The auto-brightness service no longer adds load: it writes only real changes, 6-step fades (0 errors on start).
  - Timing margin tested 01:40: panel hbp 652 (link 766 Mbit/s): 4/4/1; hbp 452 (633 Mbit/s): 1/0/5 - no effect.
  - Next ideas: (1) [done, no effect] timing margin - the 2026-11-05 boot-time HS-lane underflows were cured by `hbp=552` (link vs
    pixel-clock balance); try a slightly larger hbp / pclk so the MDP supply has headroom while a command steals link
    time; (2) send brightness in HS mode inside the vertical blank (TE interrupt) like Android's commit-path timing;
    (3) capture Android's timing (DMA trigger vs frame) with dyndbg. Clean series unchanged (b5de5d119).

- 2026-11-05 18:05: **the cleaned, upstream-shaped panel driver (docs/upstream/0003, no module params) runs both panels** on one-shot kernel #80 (with patches/msm-dsi-dsc-all-slices-flag.py, the upstream DSC_ALL_SLICES_IN_PKT backport): clean picture on both, 0 DSI errors. Build any panel file with `PANEL_C=<file>` (apply-dts.sh).
- 2026-11-05 11:50: upstream check (torvalds/master 7.4-rc1): (1) command-mode BURST_MODE is still set for every 6G >= v1.3 -> RFC drafted (docs/upstream/0002, not sent); (2) **our slice-per-packet patch is superseded upstream**: `MIPI_DSI_MODE_DSC_ALL_SLICES_IN_PKT` (BIT 13) makes msm use dsc_slice_per_pkt = slice_count - on a kernel rebase the Duo panel driver sets that flag and patches/msm-dsi-slice-per-pkt.* are dropped; (3) dispcc-sm8250 still lacks CLK_OPS_PARENT_ENABLE on the DSI RCGs -> docs/upstream/0001 (not sent).
- 2026-11-02 night, round 1: display round 2 — Droidian DSI debugfs read; panel off/on capture blocked (item compositor, HWC); next: boot-time dyndbg. Webcam recipe in memory. `tools/linux/keypress` (uinput key injector).
- 2026-11-03 10:05: offline — display prior art (drm/msm #24, SoMainline Xperia 1 DSC driver); `ppssel` test switch (kernel #37).
- 2026-11-03 11:15: offline — DSC_ACTIVE read-modify-write lead (display); PM8150B charger port plan (needs the tester present).
- 2026-11-03 20:15: display one-shot tests: exact DSC_ACTIVE and PPS table 0 both still noise (ruled out).
- 2026-11-03 14:25: offline — display `msm.dpu_dsc_exact` switch built (kernel #39).
- 2026-11-05 06:53: `firstdelay=1000` (vs the default 300 ms before the FIRST panel init) on kernel #76, 3 boots with
  duo.nodsifix=1: WORSE - run 1 camera-side panel static + keyboard panel dark, runs 2-3 BOTH panels dark. The settle
  delay is not the cure (keep 300). Silent dark first init PARKED again; the unconditional pre-splash re-init (initramfs
  #87) handles it on every boot. Remaining ideas: compare the DSI1 PHY/PLL state right after the first prepare vs after
  the re-init (register dump; DSI regs are safe only while the display is ON), and whether the panel needs its reset
  pulse (gpio6) longer on the cold first power-up.
- 2026-11-05 06:47: **FRAME PACING FIXED: 60 fps.** One-shot #71 + `panel_microsoft_surface_duo.hbp=552` (~700 Mbit/s
  per lane = Android's qcom,mdss-dsi-panel-clockrate; the default hbp=20 gave ~346): core_irq pp_tx_done 60.0/s,
  ctl_start 60.0/s, TE 59.8/s; kmscube "599 frames in 9.996 sec (59.92 fps)", cube rendered cleanly on the keyboard
  panel (photo), no DSI errors / underruns. -> the panel driver default is now hbp=552 (kernel #76 = build #81, otherwise
  the #71 build + default-off test knobs). Running: 3x one-shot #76 with duo.nodsifix=1 to see whether the faster link
  also cures the boot-time panel fault (the hbp=552 test boot had no DSI error).
- 2026-11-05 06:43: **Frame pacing measured (LOOP a): 30 fps on the command-mode panels.** kmscube (gfx overlay; needs
  `sleep 60 | kmscube ...`, it quits on stdin EOF) renders a steady 29.95 fps. DPU debugfs core_irq rates during the run
  (per s): TE read pointer [12,2] = 59.8 (the panel's TE is a clean 60 Hz), pp_tx_done [0,8] = 30.0, ctl_start [1,9] =
  30.0 -> one frame per TWO TE periods. The kickoff path itself is the normal one (prepare_for_kickoff waits for the
  previous pp_done, the transfer starts at TE). Most likely cause: our display fix `msm.dsi_cmd_burst=0` - without DSI
  BURST the command-mode transfer is paced by the mode's video timing, so pushing one frame takes ~a full refresh
  (~16.6 ms at the 157.33 MHz mode clock) instead of ~7 ms (19.4 Mbit DSC frame over 4 lanes x 700 Mbit/s); pp_done
  then lands after the next TE, so every frame waits for the TE after that. Fix options (one-shot tests): shorter
  horizontal/vertical blanking in the panel mode (non-burst transfer finishes well inside 16.6 ms; earlier `hbp=552`
  wedged once at 700 Mbit/s), a higher DSI link rate for the same mode, or burst mode for the frame data only (burst
  on broke both panels on 2026-11-03, so only with the MISR check). Not needed for function - only for smoothness.
- 2026-11-05 06:30: L17A 3.0 V RULED OUT for the boot panel fault: 3 one-shot boots of #71 + #87 + test-dtb-venus-usbc-l17
  (ldo17 measured 3000000 uV, enabled) with duo.nodsifix=1: DSI errors on all 3 (2.36 s, 2.46 s, and 15.38 s at the
  brightness re-send) and the camera-side panel dark / green static / dark. The dtsi stays as a test file only; the
  unconditional pre-splash re-init (initramfs #87) remains the fix in the default boot. Also seen: the default boot
  that followed a Droidian reboot hung at 01:09 with no USB at all (no log; one case so far - watch for it).
- 2026-11-04 22:40: **Boot panel fault - more splits, root cause PARKED (workaround in the default).** All with duo.nodsifix=1 +
  webcam: `reinit=1` (in-kernel second init): 3 boots, both panels dark whenever a DSI error hit, fine without -> no fix.
  Kernel #74 (`enable_bl=0`, no brightness write in panel enable): errors on 2 of 4 boots (one at 1.89 s, before fb0)
  -> the brightness collision is ruled out. `duo.nosplash=1`: 2 boots, no errors logged, camera-side panel dark once and
  fully static once -> not the splash. Pre-venus DTB (55-haptics) on kernel #71: dark once without an error, fine once
  despite errors -> not venus / the usbc nodes. Also ruled out before: seq=0, DPU max bandwidth (#73).
  So: the DSI0 (camera-side) panel's FIRST init fails on most boots, sometimes with logged HS-lane FIFO underflows,
  sometimes silently; a re-init always fixes it -> initramfs #87 re-inits unconditionally before the splash (default).
  Open ideas: bisect the kernel back to #48 (display recipe 3/3 good on 2026-11-04 morning); dump DSI0 host + PHY state
  at handover (UEFI ran DSI0, so it is warm) vs after the re-init; firstdelay also for DSI0.
- 2026-11-04 22:25: **Boot panel faults - test results + new default initramfs #87 (1a824d7f).** With the repair off
  (duo.nodsifix=1, webcam each boot): (1) `seq=0` (Android order, all LP): FIFO error still happens (DSI0 0x88881310), and
  the camera-side panel stayed dark -> init order is not the cause. (2) Kernel #73 = #72 + patches/msm-dpu-perf-fixed-param.py
  (`msm.duo_perf_fixed=1`: DPU FIXED mode, 460 MHz core clock, 7 GB/s votes from boot): errors on 2 of 4 boots -> bandwidth
  is not the cause either. One error landed at 15.27 s on DSI1, exactly at init's brightness re-send = a HS DCS command
  sent while frames stream; the boot-time errors may be the same collision (panel enable re-sends brightness since today).
  (3) On ALL 4 repair-off boots the camera-side panel (DSI0) stayed DARK, also without any DSI error, and a brightness
  re-send did not light it; only the re-init (fb0 blank cycle) does. So init #87 runs the pre-splash blank cycle
  UNCONDITIONALLY (plus the error-triggered ones). Verified: one-shot #71 + #87, both panels whole. Root cause of the dark
  first init (DSI0, the panel the UEFI ran) still open; next test: panel_microsoft_surface_duo.reinit=1 (in-kernel
  re-init in the first prepare) and moving the brightness re-send out of panel enable.
- 2026-11-04 22:05: **Boot-time DSI FIFO error decoded** (kernel #72 = #71 + patches/msm-dsi-fifo-status-log.py): `DSI0
  FIFO_STATUS=0x88881010` = HS FIFO UNDERFLOW on all four data lanes (bits 19/23/27/31) + LP FIFO empty (bit 12) + bit 4
  (undocumented) at 2.8 s, i.e. a high-speed transfer starved mid-packet during the first panel init. Our default
  `seq=2` (sw43408 order) sends the 128-byte DSC PPS in HS mode; downstream sends the whole panel-on, PPS included, in LP
  (docs/research/34-display-investigation.md "Downstream panel-on, captured"). A truncated PPS fits the symptom (only the first DSC slice decodes).
  Hypothesis: early in boot the command-DMA fetch is too slow (interconnect sync_state still pending) for an HS long
  packet. Test running: `panel_microsoft_surface_duo.seq=0` (Android order, all LP) x4 one-shot boots with
  duo.nodsifix=1 + webcam photos. (#72 boots: error on 1 of 3; #71: 4 of 5.)
- 2026-11-04 21:45: **Half-garbled panel at boot fixed (initramfs #83, default).** the tester's "quarter of a garbled screen" = the
  upper (camera-side) panel showing only its FIRST DSC slice: drmfill bars drew the left half, the right half stayed static.
  The boot log had `dsi_err_worker: status=4` (DSI_ERR_STATE_FIFO; next boot 0xc = FIFO + MDP underflow) during the first
  panel init. A fb0 blank/unblank cycle (re-runs the panel init) fixed it live; init now does that once, ~19 s into boot,
  when dmesg shows dsi_err_worker (off: duo.nodsifix=1), then re-sends brightness. Verified: one-shot #68 + #83 hit the
  error, the repair ran, and the webcam showed both panels whole. #83 = #82 with only /init swapped (no modules from the
  #70 test tree). Root cause (why the first init overflows the DSI FIFO) still open; the repair hides it.
- 2026-11-04 14:50: **TOUCH PER SCREEN WORKS** (grim (local file, not published): the tester tapped Q P Z M on the keyboard -> 'qpzm' in the terminal). The real causes: (1) sway 1.10 IGNORES libinput's WL_OUTPUT for touchscreens - each digitizer spanned the whole stacked 900x1350 layout (bottom of the top screen typed on the keyboard, top of the keyboard hit the top screen); both iptsd devices share one sway identifier, so map_to_output cannot pick one -> per-node LIBINPUT_CALIBRATION_MATRIX via udev: top screen '1 0 0 0 0.5 0' (upper half), keyboard screen '-1 0 1 0 0.5 0.5' (lower half, x mirrored - the tester: 'keyboard is reversed left side right side'); (2) the kernel's raw single-touch hid-input devices (spi_045E:0C01) also fed sway, unmapped -> `input "1118:3073:spi_045E:0C01" events disabled` in sway-duo.conf; (3) stale udev db -> duo-desktop.sh always re-runs `udevadm test` on each touch node before sway. Top screen x-orientation still to confirm by a tap at a named spot.
- 2026-11-04 14:15: **SPAN BOTH SCREENS WORKS** (photo (local file, not published); LOOP d1 'when it launches an android app we can show hide the keyboard and it will span both screens'). sway cannot fullscreen one window over two outputs, but a FLOATING window straddling both is drawn on both: outputs stacked like a laptop (sway-duo.conf: DSI-2 position 0 0, DSI-1 position 0 675 = 900x1350 logical; BOTH positions explicit - setting only DSI-1 made sway move DSI-2 to 900,675), `floating_maximum_size -1 x -1` (the default cap clamped a span to 884x643). tools/linux/desktop/dock/duo-span.sh [on|off|toggle] [criteria]: hides the keyboard, floats the window 834x1350 at 0,0 (dock strip stays free) / back: tiles it on workspace 1 + keyboard back. Dock button 'span' (teal icon) + Super+s. Gotchas: floating enable + resize in ONE command list resized before the float (882x641) -> two swaymsg calls; un-floating drops the window on the output under its corner (keyboard screen, 15 px tall) -> move container to workspace 1. Checked on/off/on with sway's tree (829x1331 after borders). For Waydroid: duo-span.sh on 'app_id="Waydroid"' (multi-window off, full UI) once the tester has started it.
- 2026-11-04 12:15: **STANDARD LAPTOP KEYBOARD ON THE DUO** (photo (local file, not published)): wvkbd-duo built on the phone by the tester, binary kept on sda10 /opt/bin/wvkbd-duo; sway config uses it.
- 2026-11-04 11:50: DESKTOP RUNS on both panels (kernel #57): the panel driver now re-sends the brightness in every enable (a re-initialised panel kept its reset brightness - the root cause of every "dark panel" today); no power-cycle hack needed (a scripted one crashed sway: "Page-flip failed"). Layout per the tester: keyboard (wvkbd, -l full,special) on DSI-1 = the screen WITHOUT the camera, foot terminal on DSI-2; orientation locked, rotation buttons dropped. Open: touch mapping (both iptsd touchscreens share one sway identifier), VNC-style trackpad + zoom.
- 2026-11-04 11:20: **DARK PANEL SOLVED (initramfs #63):** DSI MISR (DRMFILL_CYCLE=1 so frames flow) was identical on both links (0x1f12a03c) → the dark panel received frames; its boot-time brightness command was lost. Re-writing the backlight lit it at once → init re-sends brightness at 4 s and 14 s (v-1 then v) → hands-free boot: BOTH panels lit (BRIO). kmscube then: 900 frames in ~6 s (~150 fps, exit 0, legacy and atomic) but the panels keep showing the console → page flips on the command-mode panels complete without the frame reaching the panel (TE/vsync for cmd mode?); drmfill (setcrtc) shows fine. Next: msm cmd-mode flip path (TE gpio in the Duo DT?), then a compositor.
- 2026-11-04 07:50: **left-panel race solved**: cold DSI1 link at first power-up needs time → panel `firstdelay` 300 ms default (kernel #48); 3/3 plain boots show both panels; no kick needed. Display done.
- 2026-11-03 23:46: display minimal recipe found (burst off + 2 slices/packet + non-continuous clock; dsc_single/dsc_exact/seq not needed) → kernel #44 built, not installed: the 23:31 default boot (kernel #43 + initramfs #54) wedged with NO USB at all, screens dark, watchdog did not reset → the tester power button (Morning list).
- 2026-11-03 23:20: left panel (DSI1) boot-init race found — same stream as DSI0, a CRTC off/on cycle fixes it → init does one at boot (initramfs #54); 3/3 boots show both panels. GRUB TEST entry + duo-grub-test.sh. Watcher raised to 60 boots/hour.
- 2026-11-03 22:55: **DISPLAY WORKS** — both panels, kernel console + test patterns. Cause: DSI command-mode BURST_MODE (MDP_CTRL2 bit 16) that 6.19 sets and Microsoft's kernel/UEFI don't; found via DSI MISR (UEFI vs ours black frame). Kernel #43 `msm.dsi_cmd_burst=0`; GRUB default = display on.
