# Workbench — Storage (UFS)

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)). Text moved unchanged from leg 33 on 2026-11-05; newest log entries first.

#### UFS storage stability (2026-11-02)
Symptom: heavy writes (a 5.7 GB copy) stalled after a few hundred MB; later even `mount` hung. Log: repeated
PHY-adapter link errors `pa_err[n] = 0x80000001` from ~1 s after boot, `saved_uic_err 0x44`, the UFS error handler,
then a TSTBUS register dump; earlier also `pwr ctrl cmd 0x17` (hibern8 enter) timeouts.
- Disabling power saving (`patches/ufs-qcom-duo-nopm.py`: no clock gating/scaling, power collapse, autosuspend,
  auto-hibern8 on `microsoft,surface-duo`) removed the hibern8 timeouts but **not** the link errors.
- **Fix: cap the link at HS-Gear 1 rate A** (`dts/sm8150-microsoft-surface-duo-workbench.dtsi`:
  `limit-hs-gear = <1>; limit-gear-rate = "rate-a";` — mainline's default is G3). A 2 GB `dd … conv=fsync` then
  completed with zero UFS errors.
- **2026-11-04: HS-G3 rate B works** (Android/downstream sets no gear limit = Qualcomm default G3 rate B). One-shot
  test then default (DTB #52): link `HS_GEAR3, 2 lanes, FAST_MODE, HS_RATE_B`; O_DIRECT reads 813–824 MB/s (G1 rate A:
  211.6 MB/s), a 2 GB `dd conv=fsync` file write on sda10 199.5 MB/s, 0 UFS errors. The 2026-11-02 errors were a
  rate-A problem. Power saving (gating/hibern8) stays off — a separate test.
- Never toggle UFS sysfs PM knobs at runtime on this board — that itself triggered a hang.


## Log

- 2026-11-07 17:00: **The machine check is gone: the board DT pins the UFS clocks at 300 MHz** (`freq-table-hz`
  min = max for core_clk + core_clk_unipro). devfreq then has nothing to scale, so no gear switch ever happens, and the
  fix also holds on the ABL boot path (ABL replaces the root compatible, so the machine check stopped matching there).
  Tested: UEFI one-shot + ABL boot, ufssoak 3 rounds each, 0 UFS errors. Series commit "arm64: dts: qcom:
  sm8150-microsoft-surface-duo: keep the UFS clocks at 300 MHz" replaces "scsi: ufs: qcom: no clock scaling on the
  Surface Duo". (no-windows-layout.md, "test kernel -ufspin".)

- 2026-11-07 14:30: **UFS POWER SAVING WORKS - only clock scaling breaks the link.** Test kernel with a bitmask
  `ufs_qcom.duo_keep` (test-only, never shipped; built in WSL /root/linux-ufs-src + /root/build-ufs) and init flag
  `duo.ufspm=keep` (so init no longer zeroed the sysfs knobs). One-shot RAM workbench with `duo_keep=0xf3` = every cap
  EXCEPT CLK_SCALING + WB_WITH_CLK_SCALING: caps 0x2cb, auto_hibern8 150 ms, clkgate on, rpm/spm level 3, no clkscale
  node. Boot, 90 s idle (the watchdog's 5 s O_DIRECT read = a gating/hibern8 exit+enter every cycle), 1 GiB O_DIRECT
  read (528 MB/s right after idle), then 3 rounds of 512 MiB fsync write (194.6 / 197.6 / 199.7 MB/s = the
  no-power-saving baseline) + O_DIRECT read (640-693 MB/s) + 45 s idle with runtime_status = suspended in between:
  **0 UFS errors**. With 2026-11-05 (all caps -> UIC errors in 11 s) this bisects to clock (gear) scaling. The kernel
  commit "scsi: ufs: qcom: no clock scaling on the Surface Duo" replaces the TEMPORARY all-off one; init no longer
  zeroes the knobs (`duo.ufspm=off` = the old settings, for emergencies). Still a board quirk (no DT property for
  it) - a candidate to discuss upstream (is it the Samsung KLUDG4UHDB part or this board's PHY at lower gears?).

- 2026-11-05 06:37: **UFS power saving re-test (LOOP e) - still unsafe.** Kernel #75 (build #80) = #74 tree +
  patches/ufs-qcom-duo-pm-param.py (`ufs_qcom.duo_pm=1` keeps mainline's power-saving caps; default off). One-shot boot
  with the knob: init still wrote auto_hibern8/clkgate/clkscale/rpm/spm = 0 at 2.0 s, yet at 11.4 s the link threw UIC
  errors (pa_err 0x80000003, dl_err 0x80004000 + 0x80008002, error handler, BKOPS status query err 15), and the first
  O_DIRECT read load afterwards stalled the disk -> watchdog reset (self-healed, default boot fine, 0 UFS errors).
  So even without hibern8/gating, one of the other caps (AGGR_POWER_COLLAPSE, AUTO_BKOPS_SUSPEND, WB_WITH_CLK_SCALING,
  RPM_AUTOSUSPEND, or devfreq scaling before init's write) breaks the HS-G3 rate-B link. Power saving stays OFF.
  If it is ever worth it: bisect one cap at a time (a knob per cap), not all at once.
- 2026-11-04 12:25: **UFS HS-G3 rate B** default (DTB #52): reads 4x faster (820 vs 212 MB/s), 2 GB fsync write OK, 0 UFS errors.
