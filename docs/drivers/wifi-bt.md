# Workbench — Wi-Fi and Bluetooth

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)). Text moved unchanged from leg 33 on 2026-11-05; newest log entries first.

#### Wi-Fi bring-up (2026-11-03, kernels #34–#35)
- Stack (all public pieces): mainline `ath10k_snoc` (built in, + `FW_LOADER_COMPRESS_ZSTD`), Alpine `iw`,
  `wpa_supplicant`, `wireless-regdb`, `linux-firmware-ath10k` (WCN3990 firmware-5/board-2 .zst), Alpine **edge**
  `rmtfs` + `tqftpserv` + `qrtr-libs`, in-kernel pd-mapper. Proprietary modem/WLAN firmware comes from the phone's
  own `modem_a` partition at boot (init mounts it read-only and links `image/*` into /lib/firmware/qcom/sm8150/
  microsoft/; `bdwlan.bin` → ath10k `board.bin`); `remoteproc_mpss` → `modem.mdt` (split .bNN). rmtfs runs `-r`.
- Memory map: the Duo's Android SM8150 DT (dtb fdt11) matches mainline's carve-outs; Android allocates rmtfs
  dynamically, so mainline's fixed 0x89b00000 is fine. (fdt00 in that dtb is an APQ8016 board — ignore it.)
- Supplies match Android exactly (L1A 0.752, L7A 1.8, L2C 1.304, L11C 3.0–3.312); L10C (WCN ch1 on the HDK) is
  unused on the Duo. Android names no RF clock for WLAN/BT.
- Status: **MPSS boots**; ath10k probes. WLAN firmware then crashes in a loop:
  1. with rails off at modem boot (ath10k enables them only after QMI): `ccpm_hw_clk.c:666: CCPM: PLL lock failed`;
  2. with the four rails `regulator-always-on` (workbench dtsi): first `wcss_fw_direct_wakeup.c:83: MAXIMUM POLL
     COUNT REACHED FOR WCSS_WLAN1_GDSCR.PWR_ON`, then the PLL error again → the SoC-side WCSS power domain does not
     power up. Next: compare Android's WCSS/MX/CX votes (rpmh power domains, `qcom,smp2p` wlan, icnss
     `qcom,wlan-msa-fixed-region`), and the 2026 WCN399x pwrseq series (`patches/wcn399x-pwrseq/`).
- Side effect: the modem crash/recover loop coincides with RCU stalls (~60–75 s, one CPU stuck, no NMI) that wedge
  SSH/serial. DA7280 haptics disabled in the workbench DT as a test — stalls persist, so likely the crash loop.
  Workaround while debugging: `duo.nowifi=1`, or disable remoteproc recovery.
- Display driver is OFF in the default GRUB entry while the tester is away (`initcall_blacklist=msm_drm_register`; its
  frame-overflow storm starved USB networking). Restore: `cp /duo/grub.cfg.display /duo/grub.cfg` on the ESP.
- **rmtfs address (2026-11-03):** Droidian's uio "rmtfs" = **0xfa601000, 2 MiB**; the workbench DT now deletes
  mainline's 0x89b00000 `rmtfs_mem` and reserves it there (as sm8150-mainline does for OnePlus 7 / nabu). Result: QMI
  gets further (`qmi chip_id 0x30224 chip_family 0x4001 board_id 0xff soc_id 0x40060000`), but WLAN fw still crashes
  on `WCSS_WLAN1_GDSCR.PWR_ON` then `CCPM: PLL lock failed`. Next ideas: (1) RF/XO clock — check `rf_clk1/2`
  state live and try RPMH_RF_CLK1; (2) reserve Android's `cdsp_sec_regions` 0xa4c00000/60 MiB and splash 0x9c000000/
  36 MiB like nabu; (3) board_id 0xff → calibration variant / bdwlan; (4) WCN399x pwrseq series.
- **Safety:** the crash loop wedges USB even with the watchdog fed and `panic_on_rcu_stall=1`, so Wi-Fi is now
  **opt-in** (`duo.wifi=1`) and a crash limiter turns modem recovery off after 2 crashes. (initramfs built, NOT yet
  installed — phone wedged at 08:43.)
- [ ] Phone wedged 08:43 (Wi-Fi crash loop). Please force-restart (hold power ~15 s); the watcher boots it. Then I install the opt-in-Wi-Fi initramfs (tmp initramfs.cpio.gz, md5 in the next commit).
- **Windows' view of WLAN (Duo DSDT, `\_SB.AMSS.QWLN`, HID under AMSS = modem subsystem; offline discovery while
  the phone was wedged):** resources = WCSS `0x18800000`/8 MiB, MSA `0x8BC00000`/1.5 MiB (= mainline `wlan_mem`),
  plus **`0x0C250000`/16 B** (not in mainline's wifi node — identify next); `_DEP` PEP0 + MMU0; `WRST` reset is a
  no-op. PEP package `WBRC`: D0 votes `/arc/client/rail_xo` = 0x80 (keep the XO rail on) and LDO1_A 0.752 V
  enabled (mainline already has L1A at 0.752 V HPM); D2/D3 drop both. BT = `BTH0` HID QCOM0571 on UART `\_SB.UR18`
  (QUP SE?) at 115200 + flow control, host-wake GPIO on PM01.
- **WCN3998 power sequencing + Bluetooth (2026-11-03, kernel #36, built offline while the phone was wedged):**
  Windows' BT (`BTH0`, QCOM0571) is on `\_SB.UR18` = QUP **SE13 @ 0xc8c000** — the same UART the upstream SM8150-HDK
  WCN3998 patch uses. Applied Dmitry Baryshkov's series (`patches/wcn399x-pwrseq/`: v2 01 binding, 05 pwrseq driver,
  09 `uart13`; v3 1–3 BT pwrseq, WCN3998 ROM version, **ath10k snoc powers the chip through pwrseq**). Duo DT: a
  `qcom,wcn3998-pmu` node (vddio S4A, vddxo L7A, vddrf L2C, vddch0 L11C; no ch1, no sw_ctrl/enable GPIO on the Duo),
  `&wifi` supplies from the PMU, `&uart13` + `qcom,wcn3998-bt`. Kernel: `POWER_SEQUENCING_QCOM_WCN`, `BT_HCIUART_QCA`,
  `SERIAL_QCOM_GENI`; initramfs: `linux-firmware-qca` (crbtfw21/crnv21) + `bluez`. `0x0C250000` (QWLN's 3rd resource)
  = MPM/sleep-counter area next to tsens/PS_HOLD → timestamps, not power. Untested — install pending.
- [ ] After the reset: install `(local file, not published)` (md5 eca48985…, c848ef98…, 7c076491…; #38 = #36 Wi-Fi/BT + #37 ppssel + sensors) with `duo-install-ssh.sh`; then test boots `duo.wifi=1` and `duo.sensors=1` one at a time.


#### Wi-Fi/BT live round, evening 2026-11-03 (kernels #39–#40) — PARKED with next ideas
Safety first: **USB-alive watchdog** (feeder stops petting when `usb0` rx stalls 60 s) proven — simulated USB death
recovered in 2:40, a real WLAN wedge in ~5 min; **one-shot flags** (`/duo/oneshot.flags`, read + deleted by init) so a
risky boot can't loop. Wi-Fi tests no longer need the tester's hands.
Results with `duo.wifi=1`:
- pwrseq PMU probes; BT `hci0` appears on uart13 but **never answers** (`command 0xfc00 tx timeout`, 3 retries).
- WLAN fw crashes as before (`wcss_fw_direct_wakeup.c:83 MAXIMUM POLL COUNT … WCSS_WLAN1_GDSCR.PWR_ON`, then CCPM PLL).
- Ruled out: wlanmdsp.mbn (tqftpserv `-d`: the phone's own 4,334,848-byte file is served); RF clock (Droidian with
  working Wi-Fi has rf_clk1/2/3 all at enable count 0 — RF_CLK2 on the PMU changed nothing, removed); rails (Droidian:
  L1A 0.752, L7A 1.8, L2C 1.304, L11C 3.312, all on — same as ours); WLAN MSA region (Android `pil_wlan_fw_region` =
  0x8bc00000/1.5 MiB = mainline `wlan_mem`); host-cap QMI (`qcom,snoc-host-cap-skip-quirk`, 2026 v5 series applied —
  same crash).
- Reading: BT is silent too and talks to the chip directly over UART → the WCN3998 itself is not running (power-on
  sequence / reset / XO), not a WLAN-firmware or QMI issue.
- **Found 19:15 (offline): `uart13` had NO pinctrl** (sm8150.dtsi has none; the HDK/nabu boards add `qup_uart13_default`) → BT
  UART pins never muxed = the BT tx timeouts. Added to the Duo DT (DTB #42). BT silence was likely this, not the chip.
- Next ideas: (1) on Droidian, dump the TLMM + PMIC GPIO states (`/sys/kernel/debug/gpio`, pinctrl) and diff with ours —
  a WCN enable/XO-select pin the DT never names; (2) Droidian's BT power driver log (`btpower`/`bt_power` in dmesg) for the
  exact power-on order and delays; (3) check RF_CLK2 *pin-control* mode (XO buffer enabled by the WCN's own clk-req pin,
  which would explain enable count 0 on Droidian) vs mainline rpmh-clk's software vote.
- **2026-11-04 (Droidian, Wi-Fi up there):** (1) done — **no Wi-Fi pins at all**: Droidian's TLMM claims only 8 pins
  (panel resets 5/6, CCI 17–20, fingerprint 95, codec reset 143) and the PMICs only PM8150 gpio6 (keys); the WCN3998
  has no SoC/PMIC GPIO → pin lead closed. Downstream `qcom,icnss@18800000`: supplies `vdd-cx-mx` = **PM8150 L1 0.752 V**,
  `vdd-1.8-xo` = L7A, `vdd-1.3-rfa` = L2C 1.304 V, `vdd-3.3-ch0` = L11C 3.104–3.312 V; `qcom,wlan-msa-memory` 1 MiB at
  the fixed region; iommus SID 0x640. **All match ours** (upstream Duo DT: `vdda_wcss_pll` IS `vreg_l1a_0p75`; we keep
  all four always-on, ch0 ≥ 3.304 V) → supply lead closed. Remaining: (2) downstream QMI/WLFW sequence via boot-time
  dyndbg for `icnss*.c` + `wlan_firmware_service*` (what the host sends before WCSS powers up, vs ath10k_snoc's
  host-cap / wlan-cfg / wlan-mode), (3) RF_CLK2 pin-control mode.
- **2026-11-04 09:00 — downstream WLFW sequence captured** (Droidian, `dyndbg="file icnss* +p; file wlan_firmware_service* +p"`):
  icnss probe reads the 4 supplies (no XO pin clock) → registers for PD `msm/modem/wlan_pd` (instance 180) → modem up →
  PD up (0x1fffffff) → WLFW server (node 0 port 95) → rails on → ind-register → MSA info (2 regions 0x8bc00000/0x4000
  secure + 0x8bc04000/0x17c000) → hypervisor assign HLOS → MSS_MSA+WLAN(+WLAN_CE) → MSA ready → capability (chip
  0x30224, fw 0x320104c6) → **dynamic feature mask 0x1** → BDF via cnss-daemon (bdwlan.bin) → Pin Connect Result → FW
  ready → ini/config/mode 0. Mainline matches all of it (PD mapper has sm8150 wlan_pd/180; MSA perms identical) except
  the feature mask → `patches/ath10k-dyn-feature-mask.py` (kernel #51, `ath10k_snoc.dyn_feature_mask=1`): accepted,
  but the firmware already had it ("prev 1, curr 1") → **ruled out**. Same crash, now with the full reason:
  `err_qdi.c:1024:EF:wlan_process … wcss_fw_direct_wakeup.c:83 MAXIMUM POLL COUNT REACHED FOR WCSS_WLAN1_GDSCR.PWR_ON`
  (the "String len 114 >= Max Len 65 / failed to decode" just before is this crash reason arriving over QMI).
  Runtime rails verified ON: L1A 752 mV, L7A 1.8 V, L2C 1.304 V, L11C 3.304 V.
- **New safe Wi-Fi rig:** `tools/linux/duo-wifi-try.sh` (plain boot; partition-label links, fw links, rmtfs/tqftpserv,
  modem recovery OFF before the first start) → the WLAN crash stops the modem once, **no wedge, no power button**.
- 🅿 Still parked. Left: (3) RF_CLK2 pin-control mode (downstream never votes rf_clk; does RPMh leave the WCN XO buffer
  in pin-control for us?), (4) smp2p WLAN bits (downstream `qcom,smp2p_map_wlan_1_in/out`; mainline wifi node lacks
  `qcom,smem-states`?), (5) a full RPMh/AOP vote diff while WLAN starts (Droidian `rpmh` debugfs, CX/MX arcs).
- **✅ WI-FI WORKS (2026-11-04 09:00, initramfs #57).** Idea (3) RF_CLK2: `&wifi` clocks deleted (DTB #45) → same crash,
  ruled out. Then the timelines lined up: our crash came ~3 s after capability = downstream's BOARD-DATA download, and
  downstream's cnss-daemon sends the phone's own `bdwlan.bin` (modem_a, 26 KB). Ours: init links that as `board.bin`,
  but linux-firmware-ath10k ships a generic `WCN3990/hw1.0/board-2.bin`, which ath10k tries FIRST (board name
  qmi-board-id=ff / chip-id 30224 matches a generic entry) → the radio got generic calibration → the WLAN firmware died
  powering up WCSS. **Removing board-2.bin (RAM) → firmware up (api 5, wmi-op 4), `wlan0`, scan: 27 networks (10 ×
  2.4 GHz, 17 × 5 GHz).** init now drops board-2.bin* and starts the modem with recovery OFF (no wedging loop ever);
  `duo.wifi=1` boot: modem running, 0 crashes, 20 networks. **MAC (initramfs #58):** ath10k finds no MAC ("choosing
  random"); init now reads the phone's own `wlan_mac.bin` (`Intf0MacAddress=<12 hex>`, persist, mounted read-only — the
  value is never stored in the repo or logged) and sets it on wlan0 → `addr_assign_type=3`, factory (non-local) MAC, scan
  still 20 networks.
  **Minimal recipe (DTB #46, 2026-11-04 09:11):** one-shot DT tests (`DTB=test.dtb duo-grub-test.sh "duo.wifi=1"`, test DTBs
  made from #45 with `fdtput`): no host-cap skip quirk → works (22 networks); plus upstream's RF_CLK2 + qdss clocks
  back → works (23). So both were red herrings; the upstream `&wifi` node + the Duo's supplies is all the DT needs.
  DTB #46 (from source) as default: Wi-Fi up, 0 crashes, 20 networks, both panels connected.
  **Default-on (initramfs #59):** init adds duo.wifi=1 unless `duo.wifi=0`; a plain boot → Wi-Fi up, 20 networks.
  ⚠ The first `duo.wifi=0` one-shot boot (09:14) HARD-HUNG (screens dark, no USB, watchdog didn't fire) — the
  wifi=0 path is the old plain path. **Not Wi-Fi:** the one-shot file was still on the ESP after the hang, so it hung
  BEFORE init read it (~2 s) = an early-boot hang (UEFI/GRUB/kernel), not the Wi-Fi code; the re-run after the tester's
  reset booted fine with duo.wifi=0 (modem offline, no wlan0, ADSP running). Off switch verified. To re-test for a minimal recipe: host-cap skip quirk,
  `&wifi` clock deletion, rails always-on (all added while chasing this).
  Upstream note (draft only): linux-firmware's WCN3990 board-2.bin has no Surface Duo entry; the proper fix is
  `qcom,calibration-variant` in the Duo DT + the Duo's board data added to board-2.bin (needs Microsoft's permission
  to redistribute bdwlan.bin) — or document that Duo users must use the phone's own bdwlan.bin.


#### ✅ Bluetooth works (2026-11-03 evening, DTB #43 + initramfs #48)
- Root cause of the BT silence: `uart13` (QUP SE13, gpio43–46) had **no pinctrl** — sm8150.dtsi defines none, boards
  (SM8150-HDK patch, Xiaomi nabu) add `qup_uart13_default/sleep`. Added → `QCA SOC Version 0x40010224, ROM 0x1001`,
  `crbtfw21.tlv` + `crnv21.bin` download, "QCA setup on UART is completed".
- Controller stayed **unconfigured** (no BD address from the Duo bootloader): DT `local-bd-address` with a fixed
  locally-administered address 02:D0:05:00:00:01 (not the phone's real MAC). `btmgmt public-addr` from init hangs —
  don't use it.
- **2026-11-08: the phone's OWN address, every boot (kernel 0087 + duo-bt-addr).** A non-zero DT local-bd-address is
  re-applied by the kernel on every controller setup (hci_dev_get_bd_addr_from_property), so the address userspace set
  from persist was overwritten at the re-setup it triggers - duo-bt-addr "not confirmed" on every boot. Fix: DT
  `local-bd-address = [00 00 00 00 00 00]` (mainline convention: unconfigured until userspace gives the address) +
  duo-bt-addr sets it with `bluetoothctl`'s management menu fed on stdin (`mgmt.select 0`, `mgmt.public-addr`;
  btmgmt never answers on Debian/BlueZ 5.82), then `power on`; falls back to the old placeholder if persist has none.
  **ABL boot path:** hci0 probes at ~0.9 s, before the root is mounted -> "Failed to request file
  qca/crbtfw21.tlv (-2)", then version-read timeouts and no retry; duo-bt-addr rebinds the serdev once when the kernel
  never logged "QCA setup on UART is completed". Clean ABL boot: own address + powered ~50 s after power-on. The UEFI
  path can fail its first setup too ("QCA Failed to send TLV segment (-110)", 2026-11-08 kernel 00088) - the same
  rebind recovers it (own address, powered at ~100 s).
  Lesson: repeated power-off/re-address cycles by hand can wedge the controller ("TLV response size mismatch", -84)
  until the next boot - test address changes from a clean boot.
- Init starts D-Bus (needs a `messagebus` user, added at boot) + bluetoothd; `bluetoothctl power on` / `scan on` find
  real nearby devices (LE + BR/EDR, −69…−104 dBm). Tool gaps fixed: `bluez-btmgmt`, `dbus` in the package list.
- This also proves the WCN3998 is powered correctly by the pwrseq PMU → the remaining Wi-Fi crash is in the WLAN
  start-up inside the modem firmware, not chip power.


## Log

- 2026-11-03 08:30: Wi-Fi stack up to MPSS boot; WLAN fw crash (WCSS GDSC / PLL). Health-checking watchdog feeder. DA7280 off (test). Display off in default GRUB entry.
- 2026-11-03 08:55: offline — Duo DSDT QWLN/PEP WLAN power votes decoded (XO rail vote, LDO1A, extra 0x0C250000 resource).
- 2026-11-03 09:30: offline — BT located (SE13, Windows UR18); WCN399x pwrseq series applied; kernel #36 + DT + initramfs with BT built ((local file, not published)).
- 2026-11-03 20:12: ✅ BLUETOOTH WORKS (uart13 pinctrl + DT local-bd-address); scans find real devices.
- 2026-11-03 14:55: offline — Wi-Fi/BT DT reviewed against the pwrseq match code: correct.
- 2026-11-04 09:25: duo.wifi=0 re-run OK (modem offline); the 09:14 hang came before init (one-shot file unconsumed) = random early-boot hang, not Wi-Fi.
- 2026-11-04 09:17: Wi-Fi DEFAULT-ON (initramfs #59, off = duo.wifi=0), plain boot verified; the duo.wifi=0 one-shot boot hard-hung → power button asked, re-test pending.
- 2026-11-04 09:11: Wi-Fi minimal: host-cap skip quirk + clock deletion both NOT needed (one-shot DT tests); DTB #46 = upstream `&wifi` + supplies, verified.
- 2026-11-04 09:06: Wi-Fi MAC: init applies the phone's own persist `wlan_mac.bin` to wlan0 (initramfs #58) → factory MAC, scan OK.
- 2026-11-04 09:05: **WI-FI WORKS**: linux-firmware's generic board-2.bin overrode the Duo's bdwlan.bin → WLAN fw crash at WCSS power-up; dropped in init (initramfs #57) → wlan0 scans 20–27 networks, 2.4 + 5 GHz, modem stable.
- 2026-11-04 09:00: Wi-Fi: downstream WLFW sequence captured — matches mainline except the dynamic feature mask (patch #51: accepted, no change); same WCSS GDSC crash; new duo-wifi-try.sh runs Wi-Fi with no wedge; still parked (RF_CLK2 pin-control, smp2p, RPMh votes left).
- 2026-11-04 08:45: Wi-Fi leads: Droidian uses no GPIO/PMIC pin for the WCN3998 and its icnss supplies (L1A 0.752 V, L7A, L2C, L11C) match ours → both leads closed; next: downstream WLFW QMI sequence via dyndbg.
