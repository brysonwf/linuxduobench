# Workbench — a clean kernel (one git branch, no patchers)

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)).

**GOAL (the tester 2026-11-07: "We need to make sure our kernel is perfect and not a bunch of patches"):** the Duo kernel
is ONE git branch on top of mainline, every change a single reviewable commit with a real message, built with
`make defconfig duo.config`. No sed-style python patchers, no files dropped into a dirty tree, no test knobs or
diagnostics in the shipped kernel, no `of_machine_is_compatible("microsoft,surface-duo")` hacks, one board DTS.

## Where it stood (2026-11-07 morning)
- `/root/linux` (WSL): v6.19 + 24 commits from `git am` (upstream series) **+ 50 uncommitted files** written by
  `apply-dts.sh`: ~33 python patchers, the panel and touch drivers copied in, Kconfig/Makefile edits by sed, 6 dtsi
  files appended to the board DTS; `build-kernel.sh` = defconfig + a sed m->y sweep + ~60 scripts/config calls.
- The default DTB was built in layers: board DTS + 6 appended dtsi + more dtsi via `build-test-dtb.sh` (camera, venus,
  usbc ...) + an `fdtoverlay` for the flash LED.
- **Reproducibility hole found:** the replay (below) showed the running kernel's Venus code had hand edits that NO repo
  patcher made (IRIS1 resets, vcodec1 power domain, core clock rate, domains before AXI). The repo could not rebuild the
  kernel that plays video. Also hand-added diagnostics (DUO_DP_TRACE, a Venus boot-timeout dump) lived only there.

## Step 1 — mechanical branch (DONE 2026-11-07)
- `tools/linux/kernel/make-duo-branch.sh`: new worktree `/root/linux-duo`, branch `duo/v6.19-replay-*` from the
  /root/linux HEAD (v6.19 + 24 upstream commits); replays apply-dts.sh in order, one commit per step (SMB5 series with
  `git am` before the charger patchers, as it really landed); then compares every touched file with /root/linux.
  Result: identical except the Venus hand edits (now committed: "media: venus: IRIS1 resets, vcodec1 domain, core
  clock rate (captured from the working tree)"), the DP/Venus diagnostics (dropped), the not-yet-built MBHC patch and
  the new camera `rotation`.
- `tools/linux/kernel/make-duo-config.sh`: `arch/arm64/configs/duo.config` = everything the #86 .config changes vs
  arm64 defconfig (721 lines, mechanical); `make defconfig duo.config` reproduces the #86 .config with 0 differences.

## Step 2 — clean the branch (each item = build + one-shot boot test)
**2a done 2026-11-07 10:00:** `tools/linux/kernel/make-clean-branch.sh` (+ one-time `clean-edits.py`) builds
`duo/v6.19-clean-*` in `/root/linux-clean`: 26 commits on the 24 upstream ones, each with a real message. Dropped
(inert at their defaults): DPU dsc_single/dsc_exact, perf_fixed, DSI FIFO log, ath10k dyn_feature_mask, WCD
dmic_clk_drv, UFS duo_pm, Venus enc_minimal/enc_skip. Became plain code: Venus work route 2 + CBR for VBR, DSI burst
off = new `MIPI_DSI_MODE_NO_CMD_BURST` set by our panel (the `msm.dsi_cmd_burst=0` cmdline is no longer needed),
hw-JEITA off = DT `qcom,sw-jeita-enable` (Microsoft's property; init/pmOS insmod falls back without the old param),
FCC/FV caps = the DT battery's maxima (charger dtsi now 3.0 A / 4.40 V). Kept, marked TEMPORARY: msm auto-snapshot
off (needs a dock unplug test to drop), UFS power saving off on the Duo (machine check; real cause open). Still
open: the board DT as one file, imx351 in-tree, IPA, spi-hid-duo vs upstream spi-hid, duo.config to intent only.
Test: `pack-test-modules.sh` = the 28 initramfs modules from the clean build as a 2nd initrd (test-extra.gz) +
test DTB (default + the charger properties) -> one-shot boot of the RAM workbench.
| Commit(s) today | Verdict | Clean form |
|---|---|---|
| dts: 6 appended dtsi + test dtsi + flash overlay | restructure | ONE `sm8150-microsoft-surface-duo.dts` (content = the deployed DTB, checked by a normalized dtc diff) |
| panel driver (docs/upstream/0003) | keep | "drm/panel: add Microsoft Surface Duo panels" (already upstream-shaped) |
| msm-dsi-slice-per-pkt, msm-dsi-dsc-all-slices-flag | keep | backport of the upstream flag + the user, as their own commits |
| msm-dpu-dsc-single, msm-dpu-dsc-exact | review | real fix or drop (test each) |
| msm-dsi-no-cmd-burst (+ `msm.dsi_cmd_burst=0` on every cmdline) | rewrite | a per-panel mode flag from the panel driver, no module param |
| msm-dsi-fifo-status-log, msm-dpu-perf-fixed-param | DROP | diagnostic / test knob |
| dispcc-sm8250-dsi-parent-enable | keep | clk fix |
| ath10k-dyn-feature-mask | review | |
| wcd934x-dmic-clk-drive | keep | |
| wcd934x-no-mbhc-duo (machine-compatible check) | rewrite | DT says "no jack" (or drop: MBHC init failing is harmless) |
| qcom-smbx-icl-override | keep | fix |
| qcom-smbx-fcc-fv-props (hard-coded Duo maxima) | rewrite | maxima from the DT battery (constant-charge-*-max) |
| smb5 series (Casey Connolly) + 90/91 | keep upstream as-is; 91 (hw-JEITA module param) -> DT property |
| ufs-qcom-duo-nopm (machine check), ufs-qcom-duo-pm-param (knob) | DONE 2026-11-07 | bisected: only clock scaling breaks the link -> "scsi: ufs: qcom: no clock scaling on the Surface Duo" -> 17:00 replaced by the board-DT clock pin (no machine check left; works on the ABL path too) |
| venus-sm8150-iris1 + 12 follow-ups + the captured hand edits | squash | "media: venus: add SM8150 (IRIS1)" (~3 commits: core+power, encoder fixes, QP-range init bug fix); DROP knobs enc-minimal / enc-skip / enc-work-route |
| msm-dp-sm8150-no-widebus, msm-dp-snapshot-unclocked, qmp-combo-orient-dp-guard | keep | fixes (unplug hang, sheared picture) |
| msm-no-auto-snapshot | review | possibly redundant after snapshot-unclocked |
| camss-sm8150 + dmamask + gcc-axi | squash | "media: qcom: camss: add SM8150" |
| imx351 (out of tree, tools/linux/drivers/imx351) | move in | "media: i2c: add Sony IMX351" |
| spi-hid-duo (Microsoft v0.9 port) next to the upstream spi-hid series | decide | two HID-over-SPI stacks in one kernel; long term = upstream spi-hid + Duo quirks |
| boot logo | keep, local-only | last commit, never sent upstream |
| ipa-v4.1 (separate tree /root/linux-ipa) | move in | "net: ipa: add v4.1 (SM8150)" once shipped |
| duo.config 721 lines | DONE 2026-11-07 | 433 lines, intent only (identical .config) + Qualcomm platform only (see 2f) |
| defconfig's 938 modules (idea, 2026-11-07) | measure first | the workbench initramfs ships 28; most of the rest are IR remotes (142), other GPUs (nouveau...), other NICs. Trimming would cut the build time a lot for public users, but pmOS loads modules on demand (dock Ethernet, USB keyboards, netfilter, filesystems): collect `lsmod` after a dock + USB session first, keep those families, then A/B with duo-health. Baseline (pmOS, Phosh up, no dock, 2026-11-07): exactly 28 modules loaded = our own set (camera, venus, charger, panel, touch, haptics, flash, pon, sound card); nftables/filesystems are built in |

**2i promoted 2026-11-08 ~12:30 PC time — 6.19.0-00086-g3feba09e3eed is the DEFAULT (GPU fault fixes).**
Series 0074-0086: Rob Clark's lazy-VM fixes from v7.3 (12 of the 17 apply to 6.19; the 5 msm_gem.c import patches need
other post-6.19 changes) + "drm/msm: Recover HW before retire hung submit" (hand-ported). gpu.md: GPU page faults under
13 glmark2 scenes 23-24 -> 0, hangs 1-2 -> 0. One-shot on Debian (+ the repo /init: card0/card1 splash) -> duo-health 0
FAIL (panels blanked for idle until woken - duo-health now says so instead of FAIL), sound, Wi-Fi. Promotion: initramfs
ba2c8b38 (29 modules + /init cda29d9e), vmlinuz.efi 3fe287d7, DTB unchanged (c15a0e11); ESP keeps *.prev (d0b7b116 /
3d7cb3e9); the ABL copy on sda10 /abl/ updated; Debian + pmOS have /lib/duo/<release>. Plain reboot: workbench on
00086, both panels, 0 DSI errors, sound card, wlan0. export-series.sh pins core.abbrev=9 (a mainline fetch had changed
every index line).

**2h promoted 2026-11-08 09:45 — 6.19.0-00073-gd671375a9fa7 is the DEFAULT (Waydroid policy routing).**
`arm64: configs: duo: policy routing for Android containers` (IP_ADVANCED_ROUTER, IP_MULTIPLE_TABLES,
IPV6_MULTIPLE_TABLES, xt mark/connmark/owner): Android's netd needs fwmark rules - without them Waydroid's Android got
DHCP but no default route. One-shot on Debian: Android pings 1.1.1.1, resolves names, the LineageOS browser loads
Wikipedia. Promotion: merge-initramfs (29 modules + the repo /init with duo.rootdir) = initramfs 3d7cb3e9, vmlinuz.efi
d0b7b116, DTB unchanged (c15a0e11); ESP keeps *.prev; pmOS + Debian have /lib/duo/<release>. Plain reboot: workbench
on 00073, both panels, 0 DSI errors, sound card, Wi-Fi; no warnings (only ramoops header decay).

**2a promoted 2026-11-07 10:27 — the clean kernel is the DEFAULT.** One-shot tests first (RAM workbench, then pmOS with
`duo.root=spare`): every module loads, both panels (0 DSI errors), touch, charger (old `disable_hw_jeita` param only
"ignored", hw-JEITA off via the DT), Venus, IMX351, sound card, Wi-Fi (26 networks), pmOS Phosh + audio + camera + Wi-Fi,
no kernel warnings. Then `merge-initramfs.sh` (#94 + the clean modules + new init = #95) and the ESP set: vmlinuz.efi
(the compressed EFI image is the default kernel now - it leaves room on the 256 MiB ESP), initramfs #95, DTB 0d3b75bc,
bootanim.gz, GRUB entries by id. First plain boot: all good; 2 DSI errors at the animation's end frame (17.2 s), repaired
by the post-splash dsifix - watch whether the animation hand-over causes them. pmOS keeps one module set per kernel
(`/lib/duo/<release>/`), so the old #86 set still works if the old Image is ever booted (with `msm.dsi_cmd_burst=0`).
Also fixed on the way: PipeWire's libcamera died on every camera close (assert in master's PipelineHandler::stop ->
`-Db_ndebug=true`), and pmOS lost audio each time.

**2g promoted 2026-11-07 17:35 — 6.19.0-00059-g99829a7c136b: no machine check left.**
"scsi: ufs: qcom: no clock scaling on the Surface Duo" dropped (non-interactive `git rebase --onto C^ C`), replaced by
"arm64: dts: qcom: sm8150-microsoft-surface-duo: keep the UFS clocks at 300 MHz" (freq-table-hz min = max). Tree
byte-identical to the tested duo/ufspin-test branch; backup branch duo/backup-before-ufspin. UEFI + ABL ufssoak 0
errors, pmOS one-shot duo-health 0 FAIL, plain boot clean. The series now has NO of_machine_is_compatible() in our
commits (MBHC = DT property, UFS = DT clock pin).

**2f promoted 2026-11-07 16:22 — 6.19.0-00059-g51500e5751f8 is the DEFAULT; duo.config intent-only + Qualcomm only.**
- `duo.config: intent only`: 722 -> 433 fragment lines, removed only lines whose removal leaves `make defconfig
  duo.config` byte-identical (1099 verified make runs) - the kernel does not change.
- `duo.config: Qualcomm platform only`: 47 other SoC platforms off -> image_size 76.9 -> 54.5 MiB (ABL limit ~60),
  vmlinuz.efi 22 -> 17.3 MiB; every Duo-relevant option unchanged.
- Our 29 commits now author `the tester <a noreply address>` + the AI note + Signed-off-by (public-release decision);
  upstream commits keep their authors; series exported again (every file's From/trailer changed once).
One-shot + plain boot: duo-health 0 FAIL; the error lines that remain were compared with the 00057 boot (identical
counts). The row "duo.config 721 lines | shrink" is DONE.

**2e promoted 2026-11-07 15:15 — 6.19.0-00057-g0fe59d4d69e9 is the DEFAULT.** + `arm64: dts: qcom:
sm8150-microsoft-surface-duo: skin thermistors and skin temperature limits` (thermal.md) + `ASoC: qcom: q6asm-dai:
close a stopped stream before preparing it again` (audio-mics.md: the EALREADY burst, A/B 264 -> 0 failures).
One-shot on pmOS + plain boot verified; known-good-55/ keeps the previous set.

**2d promoted 2026-11-07 14:35 — 6.19.0-00055-gdfe31533c7da is the DEFAULT; one TEMPORARY commit left (msm snapshots).**
- `scsi: ufs: qcom: no clock scaling on the Surface Duo` REPLACES the TEMPORARY all-off UFS commit (bisected:
  clock scaling alone breaks the HS-G3 link; gating, hibern8, auto-hibern8, power collapse, runtime suspend,
  auto-BKOPS all work - storage-ufs.md). init no longer zeroes the UFS knobs (`duo.ufspm=off` = the old settings).
- `ASoC: qcom: q6asm-dai: leave a failed prepare retryable` (new): a failed prepare left the DSP stream open or
  freed the audio client -> capture dead until a reboot after fast card-profile switches; now it recovers by itself
  (pmOS test: 3 fast profile cycles, a 6 s EALREADY burst after the next capture closed, then 3 recordings OK).
- `drm/msm: TEMPORARY - no automatic error snapshots` is still in the series: it was added because a snapshot
  aborted reading an unclocked DP block, which series commit "drm/msm/dp: skip the register snapshot while the core
  is off" now handles - probably redundant; test by dropping it and forcing an error snapshot (DSI error / DP plug).
  Checked 14:40: the DSI host snapshot is wrapped in pm_runtime_get_sync and the PHY's PLL block is read only when the
  PLL is on, but the PHY base/lane/reg blocks are read with NO power check - and an unpowered display register read
  hard-hangs this phone. No debugfs trigger exists (snapshots come only from encoder errors / IOMMU faults), so it
  cannot be proven safe by a test. Keep 0040 until the snapshot code checks power for every block it reads (the real,
  upstreamable fix, like 0039 did for DP); runtime test switch: /sys/module/msm/parameters/duo_snapshot.
One-shot on pmOS + plain boot: UFS power saving active with 0 errors, every driver up.

**2c promoted 2026-11-07 13:50 — 6.19.0-00054-g1732135a1333 is the DEFAULT.** Two new commits + two fixups:
`ASoC: codecs: wcd934x: no headset detection on boards without a jack` (DT boolean `qcom,no-headset-jack`, read from the
SLIMbus device's own node - a first version read `dev->parent`, the controller, and MBHC ran again: the neighbouring
`qcom,dmic-sample-rate` read has that latent bug upstream, no wcd934x board sets it), `duo.config: nftables reject`
(pmOS's firewall failed to load without it); spi-hid-duo fixed to 0 warnings (a %s that got an int would have oopsed).
One-shot on pmOS, then a plain boot: 0 MBHC lines, firewall + the Duo allowlist loaded, 0 DSI errors, every driver up.
The series is exported with `kernel/export-series.sh` (--no-numbered: a new commit no longer rewrites every file).

**2b done 2026-11-07 11:15 — one board DT.** The board DTS includes exactly the 12 Duo dtsi files the default DTB is
made of (audio display workbench charger gpu mic camera camss imx351 usbc venus flash); the flash LED overlay became
`sm8150-microsoft-surface-duo-flash.dtsi`; dead experiments deleted. `tools/linux/dts/dtb-compare.py` (phandles paired
by node path, cell-by-cell compare): `make dtbs` = the running default DTB 0d3b75bc, identical.

## Step 3 — the repo (2026-11-07)
`tools/linux/kernel/series/` = `git format-patch v6.19..duo/v6.19-clean-*` (52 patches: 24 upstream ones we carry -
spi-hid, WCN39xx pwrseq, fastrpc remote heap, ath10k host-cap quirk - then the SMB5 series and our 28 commits).
Checked: `git am` of the series on a fresh v6.19 gives a tree identical to the branch. `tools/linux/kernel/build.sh` =
worktree at v6.19 + `git am` + `make defconfig duo.config` + Image.gz/vmlinuz.efi/modules/DTB into /root/build-duo;
initramfs-update-init.sh and imx351's build-oot.sh default to that build dir. The series files are `-text` in
.gitattributes (byte-exact on a Windows checkout). Changing the kernel now = a commit on the branch, then re-export the
series (make-clean-branch.sh / make-duo-branch.sh are kept as the record of how the branch was first made).
Upstream sends stay drafts for the tester.
