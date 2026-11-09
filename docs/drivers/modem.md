# Workbench — Modem, cellular and GPS

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)). Text moved unchanged from leg 33 on 2026-11-05; newest log entries first.

## Current understanding (2026-11-05)
- **Works:** modem boots (MPSS), QMI over qrtr (DMS/NAS/WDS/UIM/LOC), EFS via read-only rmtfs, TFTP via tqftpserv; the
  board's hardware MBN ("DSSA-LA-SM8150") can be loaded + activated with tools/linux/modem/duo-pdc-load.c.
- **GPS works (2026-11-05):** low-power mode + tools/linux/modem/duo-gps.c -> satellites tracked (fix needs sky view).
- **GPS on postmarketOS (2026-11-06):** ModemManager now drives the modem (veth stand-in net port, no IPA v4.1 yet) and
  sets up location (gps-raw/nmea/agps caps). Cold, indoors at the desk the engine finds 1 satellite in 10 min (GLONASS 14,
  28 dB-Hz; same as 11-05) while Android gets a FIX there -> Android is ASSISTED (XTRA orbit data + time). duo-gps got
  DUO_GPS_SOURCES=1 (the modem's own XTRA servers: path1..3.xtracloud.net/xtra3grcej.bin), DUO_GPS_XTRA=<file> (inject
  in 1 KB parts) and DUO_GPS_STANDALONE=1. ModemManager has the same (--location-inject-assistance-data) for apps.
  INJECTION REFUSED (2026-11-06 20:20): 'Inject Predicted Orbits Data' = NotSupported; 'Inject XTRA Data' accepts parts
  1..n-1 and fails the LAST part (status 1 = general failure, the whole-file check) for all four variants (xtra3grcej,
  xtra3grc, xtra3gr, xtra2); raw 'Inject UTC Time' (0x0038, DUO_GPS_TIME=1) also status 1. Same in low-power AND online
  (online from low-power: no crash), with 'Inform Client Revision' (0x0021, DUO_GPS_REV=1) sent first. Without
  assistance the search list grows (14 -> 32 GPS PRNs at snr 4 = searching), signals stay weak indoors.
  NEXT LEAD: Android's location HAL registers as the LOC MASTER CLIENT (QMI_LOC_REGISTER_MASTER_CLIENT in Qualcomm's
  open CAF loc_api_v02 headers) - injections from other clients may be refused; get the message id + key from the
  CAF source, then retry. Meanwhile a fix needs sky view (window) - the engine itself works.
  CORRECTION (20:50): AOSP location_service_v02.h (hardware/qcom/gps, IDL minor 4) says Inform Client Revision =
  **0x0020**; 0x0021 = REG_EVENTS, so the earlier "revision first" test was invalid (it re-registered the event mask).
  Fixed in duo-gps.c (DUO_GPS_REV=<n>, default 0x50); with a real revision announced UTC injection is STILL status 1
  (payload matches the header exactly: TLV1 u64 ms, TLV2 u32 unc). That AOSP header predates the master-client
  messages - a newer CAF header (LA.UM.8.x) is still needed for that lead; codelinaro raw URLs 404'd.
  **What the engine does now (20:50, indoors, low-power):** GSV lists 14 GPS sats WITH elevation/azimuth (the
  receiver has the constellation now, not on the cold runs), but no SNR on any = nothing tracked. Position reports
  "fix" with tech mask 0x2 = CELL ID and no uncertainty: a cell-tower position, NOT a satellite fix (duo-gps now
  prints `unc` + `tech`). So: location engine + cell positioning work on mainline; a satellite fix needs sky view.
- **Droidian A/B (2026-11-05):** online works there with every vendor QMI daemon frozen; qcrild's online request is byte-identical to ours (DMS 0x2e mode 0). So the difference is modem/RF state set up before the first online (boot-time QMI of the vendor daemons or downstream kernel drivers); a fresh-modem test on Droidian needs a modem restart, which its SSCTL refuses.
- **ONLINE WORKS (2026-11-05 18:10):** low-power first (init does it), then online -> LTE signal measured, no crash. The items below are history.
- **Was open (PARKED until 18:10):** operating mode online → `RFLM ... Assertion (rflm_qlnk_ls_retry_cnt < 2)` (RF QLink link-state
  failure) on mainline only; Droidian goes online on the same phone (no SIM inserted, none needed for this).
- **Ruled out:** EFS (same reads as Android), TFTP (same requests), the hardware MBN (needed, not sufficient),
  AP-visible regulators incl. LCX/LMX (SLPI's), RF clock buffers (not AP-voted on Droidian either), TLMM pins
  (QLink 61/62 func1) and PMIC GPIOs (identical registers), and the boot path (fails the same when ABL boots mainline).
- **Gotchas:** with remoteproc recovery ON an online crash freezes the whole SoC (keep recovery off for that step);
  reading the modem_a vfat dir that holds two "sm8150.p" entries can hang (direct paths only).

- **GPS ASSISTANCE WORKS (2026-11-07 07:47, pmOS):** the "master client" lead is dead (CAF location_service_v02.h,
  IDL minor 0x23, has no such message). What unblocked injection: SET_ENGINE_LOCK 0x003A = NONE (1) + SET_XTRA_VERSION_CHECK
  0x0085 = DISABLE (0) before injecting -> UTC time injection status 0 (was 1 every time) and ALL 47 parts of
  xtra3grcej.bin (path1.xtracloud.net, 47280 bytes) accepted (the last part used to fail). GET_ENGINE_LOCK answered
  status 0 with no lockType TLV. duo-gps: DUO_GPS_LOCK=none|get, DUO_GPS_VERCHECK=0 (+ DUO_GPS_TIME=1,
  DUO_GPS_XTRA=<file>). Not yet split which of the two did it. Indoors at the desk: 1 satellite (gps 32, 27 dB-Hz)
  within 30 s; 10-min assisted session running (log /var/tmp/gps-assisted.log). rmtfs is read-only, so nothing
  persists in the modem's EFS - redo both settings each session (ModemManager's own injection path would need the same).

## Log

- 2026-11-07 10:40 (pmOS, clean kernel, phone indoors on the desk): **no satellite is RECEIVED - the "tracked"
  satellites are predictions.** With time + XTRA injected (both accepted), a 180 s session in low-power mode lists 14 GPS
  satellites, every one at the same placeholder "snr 4" and NMEA GSV with empty elevation/SNR fields; position stays
  `in-progress`. Online mode (from low-power, safe): 19 satellites (GPS + GLONASS), same placeholder SNR; position
  reports become `fix=yes` with tech mask 0x2 = a CELL-tower position (heard towers without a SIM), not GNSS. The
  earlier note "GPS receives in low-power mode" read these predicted entries as tracking - corrected. The Duo's DT
  (dtbo dt31) has no GNSS/LNA node: the RF front end is the modem's. Next: the same spot under Droidian (the phone's own
  vendor GNSS HAL) to separate "our session setup" from "this desk is too shielded".
- 2026-11-07 10:45-11:05, Droidian (RAM boot): its vendor location stack runs (android.hardware.gnss@2.1-service-qti,
  loc_launcher, xtra-daemon, lowi-server; LocSvc injects Wi-Fi AP data into the modem every few s). Starting a GNSS
  session from Linux failed: geoclue (Droidian build with the GNSS HAL source, "Initialising GNSS interface") never let
  a plain client in - Geoclue.Simple timed out after ~25 s even with a conf.d entry allowed=true/system=true (it wants
  an agent); geoclue then exits "not used for 60 s". Gotchas: Droidian's lxc-attach passes only the first word of a
  command (`logcat -c` became a streaming logcat) - use the host's /system/bin/logcat; never `pkill -f` from the ssh
  command itself. Still open, cheapest next step: a window/outdoor session on pmOS (the tester carries the phone; duo-gps
  with time + XTRA) - if real SNRs appear there, the desk is the limit; if not, compare with a geoclue agent on Droidian.

- 2026-11-08 17:05: **FIRST SIM (a prepaid line, not yet activated) -> the QLink crash is back, and it takes
  Wi-Fi down.** Modem: SIM operator 311480, signal 88 %, network reject cause #8 "GPRS and non-GPRS services not
  allowed" (= the line is not activated; the carrier activates it through its app). Two ABL boots of Debian: ModemManager
  enabled the radio -> reject -> ~2 s later `RFLM ... Assertion (rflm_qlnk_ls_retry_cnt < 2)` -> modem crashed
  (recovery off by design) -> Wi-Fi deaf (its firmware runs on the modem DSP: ARP to the router INCOMPLETE, rx 0).
  First boot: ModemManager was D-Bus-activated (Plasma) at 27 s, before low-power -> fixed with
  `ModemManager.service.d/duo-lowpower.conf` (ExecStartPre=duo-modem-lowpower, any start incl. D-Bus). Second boot:
  low-power WAS set first (27.5 s) and it still crashed after the rejection -> online-from-low-power is NOT enough once
  a SIM makes the modem scan after a reject (no SIM on 11-05: nothing to scan). Workaround now: `nmcli radio wwan off`
  (persists; Plasma's mobile toggle undoes it) -> modem stays low-power, Wi-Fi fine. Next: once the carrier activates the
  line, retry online (no reject = maybe no scan); if it still crashes, compare the RF bring-up (QLink / RF clocks and
  regulators) with Android's (droidian dyndbg) for the bands it scans.
- 2026-11-05 18:15: **online repeat after a full PS_HOLD reset:** default boot -> init low-power (6.4 s) -> online OK again, LTE RSSI -63 dBm, RSRP -95, SNR 20 dB, no assertion. Recipe is reliable (2/2). Default stays low-power; go online on demand: `qmicli -d qrtr://0 --dms-set-operating-mode=online`.
- 2026-11-05 18:10: **CELLULAR RF WORKS ON MAINLINE - the modem goes ONLINE without the QLink crash.** The fix is the
  ORDER: every earlier online attempt was made from the boot state 'shutting-down' (and crashed with the RFLM QLink
  assertion); since initramfs #91 init first sets **low-power** (as Android's RIL does at boot), and from low-power
  `qmicli --dms-set-operating-mode=online` succeeds: mode 'online', modem keeps running, no assertion, and NAS reports a
  live LTE signal (RSSI -62 dBm, RSRP -95 dBm, RSRQ -14 dB, SNR 16.6 dB; 'not-registered-searching' - no SIM in the
  phone). Wi-Fi unaffected (23 networks). So the Droidian A/B result (online request identical) was right: the missing
  piece was the state BEFORE online, not a daemon or a driver. Next: repeat across reboots (is it reliable?), then a SIM
  (the tester's call) for registration + data (WDS / rmnet over IPA), voice/SMS later.
- 2026-11-05 11:05: Droidian A/B, part 2 - a FRESH modem boot with the daemons frozen is not reachable from Droidian:
  restart_level -> RELATED, then a QMI SSCTL restart request (service 43, node 0 port 1, msg 0x0020, sent with a python
  AF_QIPCRTR socket) is refused (result 1, error 2); downstream has no debugfs restart trigger. The re-online with the
  daemons frozen worked again. Side observation: Droidian's rmt_storage WRITES modem_fs1/modem_fs2 (2 MB each at ~106 s
  and ~207 s); mainline runs rmtfs -r, which keeps writes in an in-memory shadow (the modem reads its writes back), so
  this is a weak lead. Remaining ideas unchanged: boot-time QMI of the vendor daemons (needs tracing from container
  start), downstream RF drivers. Phone back on the workbench default, watcher active.
- 2026-11-05 11:00: **Droidian A/B, part 1: the online request itself is identical.** On Droidian (where online works),
  (a) with every vendor QMI daemon FROZEN (SIGSTOP: thermal-engine x2, dpmQmiMgr, ssgqmigd, cnss-daemon, netmgrd,
  imsqmidaemon, imsrcsd, loc_launcher, tloc_daemon, qti, adpl, port-bridge, ipacm) the modem goes offline -> online
  via ofono with no crash; (b) strace of qcrild during that cycle (tools/linux/modem/qmi-strace-decode.py + the boot's
  qrtr-lookup; raw trace local only): the ONLY DMS traffic is 0x002d Get Operating Mode + **0x002e Set Operating Mode
  TLV 0x01 = 0x00 (online)** = exactly what `qmicli --dms-set-operating-mode=online` sends on mainline; the other 98
  requests are NAS (0x0043/0x00ac/0x0031 ... = registration/system info queries after the switch). So whatever lets the
  RF (QLink) come up on Droidian is state set up EARLIER, at modem boot / before the first online, and it survives an
  offline/online cycle. Next: restart the modem on Droidian with the daemons frozen from the start (does the FIRST online
  after a fresh modem boot still work without them?), then trace the boot-time QMI of each daemon that matters.
- 2026-11-05 10:55: **GPS ready from every boot** (initramfs #91, off: duo.nolpm=1): init sets operating mode low-power once the modem runs (5.9 s), qmi-utils/qrtr + our duo-gps are in the initramfs (/usr/local/bin/duo-gps - not on the ssh PATH, use the full path). One-shot then default: Wi-Fi still scans (17 networks), GPS PRN 16 tracked within 90 s, camera + sensors + panels unaffected.
- 2026-11-05 10:45: **GPS RECEIVES on mainline.** The 10 min duo-gps run (low-power mode, phone indoors on the desk):
  after a few minutes the engine tracks **GPS PRN 16 at 26-27 dB-Hz** (GPGSV "1,1,01,16,,,27"; ~100 SV reports with
  signal in 14 min) - so the GNSS RF path works without the modem going online (no QLink needed for GNSS). One satellite
  indoors = no fix, as expected. Next: a run with sky view (window / outside) for a real fix; then gpsd/geoclue glue.
- 2026-11-05 10:40: **GPS engine runs on mainline (low-power mode); no satellites seen yet.** New facts: (1) operating
  mode **low-power** (= airplane) is safe - set over QMI, modem keeps running, no QLink assertion (only 'online' trips it);
  (2) qmicli cannot hold a LOC session (each call gets a new client; "Unknown client 1 for service loc"), so the 11-04
  "no SV info" test never had a running session. tools/linux/modem/duo-gps.c = one long-lived libqmi LOC client (register
  position/SV/NMEA events, set NMEA types, start periodic session; prints sentence TYPES only - GGA/RMC would carry the
  user's coordinates; coordinates are never printed). Result, phone indoors on the desk, 120 s: session in-progress, 119
  position reports (no fix), 476 NMEA (GGA/GSA/RMC/VTG at 1 Hz), every SV report empty, no GSV. Unknown yet: GNSS RF off in
  low-power mode (it is on in Android airplane mode) vs. no sky view indoors; a 10 min run follows. Next: a run by a window,
  then the same tool on Droidian in the same spot (A/B), LOC engine-lock/NV settings.
- 2026-11-03 08:45: rmtfs moved to 0xfa601000 (from Droidian) → QMI chip_id now reported; WLAN GDSC crash remains; Wi-Fi made opt-in + crash limiter (pending install); phone wedged.
- 2026-11-05 08:45: **MODEM PARKED (online = RF QLink assertion on mainline only).** Last two AP-side leads ruled out on
  Droidian: LCX/LMX levels (pm8150_l8_level / l4_level) are voted only by 5c00000.qcom,ssc (the SLPI), and the modem's
  own downstream votes are vdd_cx + vdd_mss (proxy, released) = mainline's; the RF clock buffers (rf_clk1-3, ln_bb_clk2/3)
  have enable count 0 on Droidian too (the modem votes them itself). Droidian's radio log: qcrild just does setRadioPower
  on -> radioState ON (QCRIL logging off). Ruled out overall: EFS, TFTP, HW MBN (needed but not sufficient), regulators,
  LCX/LMX, RF clocks, TLMM pins, PMIC GPIOs, boot path (UEFI vs ABL). Remaining ideas: (1) QCRIL/QMI trace on Droidian
  (enable QCRIL logging -> /data/vendor/radio/ril_log, or qrtr/glink ipc_logging) and replay its pre-online QMI sequence
  on mainline; (2) downstream daemons mainline lacks (qwesd/ssgtzd licensing, thermal-engine/qmi-tmd, cnss, netmgrd);
  (3) a downstream kernel driver touching RF state (grep surface-duo-oss for rffe/qlink/sdr/wtr).
- 2026-11-05 08:38: **ABL DIRECT BOOT WORKS + the modem boot-path A/B is done: QLink still fails.** Slim kernel #77
  (only ARCH_QCOM, LOCALVERSION -slim, image_size 54.5 MiB; /root/config-77slim, the default .config restored) +
  overlay-sink DTB + a 937 KB stage-1 ramdisk (tools/linux/abl-stage1: busybox + init that mounts the phone's "linux"
  partition, unpacks /abl/initramfs.cpio.gz = the full workbench initramfs into a tmpfs and switch_roots; EXTRA= bakes
  files into /abl-extra) -> ABL starts mainline directly, the workbench comes up with ABL's androidboot.* args (SSH,
  remoteprocs). ABL limits found: kernel <~60 MiB, ramdisk well below 52 MB (16.7 MB OK), DTB must accept the dtbo.
  ABL-path side issues: our init's watchdog feeding resets the phone after ~35 s (boot with duo.nowdt=1), the DPU floods
  "frame done timeout / vblank wait timed out" (ABL leaves the panels in another state), and UFS reads stall later in
  the boot (sda10 mount hung, a find on the modem vfat hung in d_alloc_parallel) -> the MBN came from /abl-extra.
  **Modem online on the ABL path: the SAME `RFLM@qsf_hl_seq.c:118 Assertion (rflm_qlnk_ls_retry_cnt < 2)` 1.7 s after
  online** -> the UEFI path is NOT the cause; the difference is downstream kernel vs mainline. Lead for next round:
  Droidian votes two RPMh rails mainline never touches - pm8150_l8_level (lcxlvl = LCX) and pm8150_l4_level (lmxlvl =
  LMX), enabled, 1 user each (regs-droidian.txt) - find their consumers on Droidian, then vote LCX/LMX from mainline
  (rpmhpd SM8150_LCX / SM8150_LMX power domains on the mpss node, one-shot DTB).
- 2026-11-05 07:55: **ABL direct boot - both blockers found.** Isolation by swapping parts (handover = the fastboot
  USB device disappears within ~10 s of "Booting OKAY"): Droidian rebuilt with tools/linux/abl-bootimg.py boots (builder
  OK); Droidian kernel + our DTB: refused; Droidian's own DTB WITHOUT __symbols__: refused -> (1) **ABL must be able to
  apply the board's dtbo overlay (dt31 "Surface Duo DV_A", board-id <8 0x10>) to the base DTB**, and it needs every one
  of the 100 labels in the overlay's __fixups__. Fix that works: an "overlay sink" - a disabled top-level node with one
  empty child (own phandle) per label + __symbols__ pointing the labels there (fdtoverlay merges dt31 cleanly; the
  overlay writes only into the disabled sink, which the kernel never populates) - Droidian kernel + sink DTB: ABL hands
  over. (2) **ABL refuses kernels above ~60-64 MiB decompressed**: our #76 Image needs image_size 76 MiB (0x4cf0000);
  Droidian's kernel padded to 76 MiB is refused, padded to 60 MiB is accepted (and our kernel with a small ramdisk +
  sink DTB + addresses moved high is still refused). Next: a slim kernel (only ARCH_QCOM instead of every vendor's
  platform; the .config now has dozens of ARCH_*=y) under ~60 MiB, then the boot-path A/B for the modem.
  (The 60 MiB padded Droidian kernel then hung on the logo -> power button; harmless.)
- 2026-11-05 07:32: ABL direct boot of mainline, try 1 (tools/linux/abl-bootimg.py: header v2 from Droidian's
  addresses; kernel = #76 Image gzipped 22.6 MB, ramdisk = initramfs #87 51.7 MB, DTB = default + qcom,msm-id
  <0x153 0x20000> + qcom,board-id <0 0>): `fastboot boot` -> "Booting OKAY", then the Microsoft logo stays and the phone
  keeps answering fastboot (ABL never started the kernel); after a failed boot ABL refuses the next `fastboot boot`
  ("Command failed") until `fastboot reboot bootloader`. Moving ramdisk/dtb clear of the 78 MB decompressed kernel
  (RAMDISK_ADDR=0x6000000 DTB_ADDR=0x5800000) did not change it. Next isolating test: rebuild Droidian's own
  kernel+ramdisk+DTB with the same tool (proves the builder), then swap one part at a time (our ramdisk, our kernel,
  our DTB) to find what ABL rejects (size limits, the dtbo overlay merge on a mainline DTB, qcom,pmic-id).
- 2026-11-05 07:30: MODEM round 5 - AP-visible pin state matches Droidian. TLMM pinmux (tools/linux/modem/duo-pinmux-dump.sh
  on both): neither kernel claims an RF pin (Droidian's extra claims: camera CCI 17-20, touch irq 42, BT uart 85/86,
  fingerprint 95 + 144-147, audio clk 148). PMIC GPIOs: downstream keeps PM8150 gpio2/5/7/8, PM8150B gpio3/4/7 and
  PM8150L gpio7 away from Linux (not registered = owned by another master); their registers on Droidian with the modem
  ONLINE (tools/linux/modem/droidian-pmic-gpio-read.sh, regmap debugfs spmi0-0N) equal mainline's /sys/kernel/debug/gpio
  view (gpio2/5 + B3/B4 disabled, gpio7s input pull-down, PM8150 gpio8 output-high func1 VIN1). Droidian has no /dev/mem
  (raw TLMM cfg of the QLink pins 61/62 not readable there). With EFS, TFTP, HW MBN, regulators and pins all equal, the
  big remaining difference is the BOOT PATH (Droidian: ABL -> kernel; ours: ABL -> SB-off UEFI -> GRUB -> kernel).
  Next: boot the mainline kernel straight from ABL as an Android boot.img (mkbootimg, DTB with qcom,msm-id/board-id so
  ABL accepts it, RAM-boot only via fastboot boot) and retry online with recovery off - a clean A/B on the boot path.
- 2026-11-05 06:57: **MODEM online retry WITH the log (tools/linux/modem/duo-modem-online-try.sh online, /proc/kmsg
  streamed to the PC).** HW MBN loaded + activated -> "DSSA-LA-SM8150 Active" after the planned modem restart; online
  set OK at 145.27 s; at 147.81 s the modem watchdog fires with the SAME `RFLM@qsf_hl_seq.c:118 Assertion
  (rflm_qlnk_ls_retry_cnt < 2) failed` -> **the hardware MBN was missing but is not the cause of the QLink failure.**
  remoteproc recovery (on for this test) restarted the modem, ath10k crashed + restarted with it, and the log stops at
  ~152 s = the SoC freeze is in the RECOVERY path (modem SSR + WLAN restart), not in the RF start (recovery off = the
  modem only crashes, 2026-11-04). Keep recovery OFF for online tests. Next ideas for QLink: TLMM pinmux diff Droidian
  vs mainline for every pin (pinctrl debugfs pinmux-pins is safe on both; an RF/RFFE/GRFC pin muxed or driven
  differently - note gpio0/1 are driven for touch, gpio2 for the FSA4480), and RPMh clocks the SDR may need
  (rf_clk*/ln_bb_clk*) - the AP votes them independently of the modem.
- 2026-11-05 01:15: **Regulator diff, Droidian vs mainline** (tools/linux/modem/duo-regulator-dump.sh; dumps in
  (local file, not published) local only). Modem: nothing usable - the modem votes its own rails through its RPMh channel
  (invisible in /sys/class/regulator); downstream BOB = 3.3 V but its consumers are only the camera sensors + the codec's
  3v3. **Display lead (new):** downstream runs PM8150 **L17A at 3.0 V** (consumer soc:qcom,dsi-display-secondary-vpnl) =
  `vpnl` of panel R2 on DSI1 = the CAMERA-SIDE screen (DRM DSI-2) - exactly the panel whose first init fails on most
  boots. Mainline: the Duo DT allows 2856000..3008000 and the core picks the minimum -> **2.856 V** (and HPM). Test DTB
  ready: dts/sm8150-microsoft-surface-duo-l17a-3v0.dtsi (min 3.0 V) -> (local file, not published)
  (cf72f530 = default venus+usbc + this). Not run yet: after the Droidian reboot at 01:08 the DEFAULT workbench boot hung
  (no USB at all, watcher "no SSH after 120 s") -> power button; and with the room dark the webcam sees nothing, so the
  test needs light anyway. Next (morning): 3x one-shot `#71 + #87 + test-dtb-venus-usbc-l17` with duo.nodsifix=1 +
  photos; if the camera-side panel comes up every time, make L17A 3.0 V part of the default DTB and drop the
  unconditional blank cycle.
  Also: other mainline-only AP votes: S5A 1.904 V, S6A 0.92 V, S8C 1.352 V enabled (downstream: not voted by the AP).
- 2026-11-04 23:00: **MODEM round 4 - the missing HARDWARE MBN.** Droidian (downstream, same phone, no SIM) puts the modem
  online fine (ofono: Online=1, no SSR) -> the mainline crash is OUR difference, not HW/firmware. Ruled out by comparison:
  EFS (Android rmt_storage and our rmtfs read the same: fs1+fs2 headers, then fs1 from offset 1024), TFTP (Android
  tftp_server and our tqftpserv get the same requests: mbn_hw.dig, mbn_sw.dig, readwrite/mcfg.tmp, server_check.txt,
  ota_firewall/ruleset), QLink pins (GPIO 61 qlink_request / 62 qlink_enable: func1, unclaimed by Linux).
  FOUND: `qmicli --pdc-list-configs=platform` = **0 platform configs** on mainline (25 software configs, one active).
  The board's hardware MBN = modem_a image/modem_pr/mcfg/configs/mcfg_hw/generic/microsof/epsilon/hw/rel/sm8150.*/common/
  mcfg_hw.mbn (62448 bytes; mbn_hw.txt names "sm8150.g", the vfat mount shows two dirs both as "sm8150.p").
  qmicli 1.36 --pdc-load-config segfaults over qrtr -> new tools/linux/modem/duo-pdc-load.c (libqmi PDC: SHA-1 id,
  1 KiB chunks on the load-config indication, select, activate; built on the phone with the dev overlay). Result:
  loaded + selected = "DSSA-LA-SM8150" platform, Pending; activate -> the modem crashes ON PURPOSE ("Modem Graceful
  Restart failed. Intentionally crash with err fatal for MCFG activation") and needs remoteproc recovery (init starts it
  with recovery OFF): debugfs remoteproc1/recovery enabled + recover -> modem up, config **Active** (rmtfs -r keeps the
  writes in RAM for the session). Then `--dms-set-operating-mode=online` -> the whole SoC FROZE past the watchdog (power
  button), instead of the old modem-only RF QLink crash: different failure = the RF now starts further. Next: repeat with
  /proc/kmsg streamed to the PC (the freeze left no log), check which rails/clocks the RF start needs (SoC-wide stall =
  an AP-visible bus/power access, e.g. an RPMh resource or a PMIC rail the AP side turned off), and compare Droidian's
  regulator states (/sys/class/regulator) with ours.
- 2026-11-04 22:50: MODEM round 3 (EFS): no SIM in the phone (the tester) -> 'no-atr-received' is expected. rmtfs (`-r -P -s`)
  serves the right partitions (by-partlabel modemst1=sdf2, modemst2=sdf3, fsg=sdf4, fsc=sdf5; all 4096 sectors, encrypted
  content, not blank). Verbose run (stop modem, `rmtfs -r -P -s -v`, start modem; tools: this entry): the modem opens
  /boot/modem_fs1, modem_fs2 and modem_fsg, reads ONE sector (sector 1) of fs1 and fs2 and then 4094 sectors of FSG ->
  it rejects both working EFS copies and boots from the factory golden copy every time (read-only rmtfs never writes a
  refreshed copy back). Next: the same capture on Droidian (Android rmt_storage: which copy does it load? does
  `ofono` online work there?) - if Droidian also loads FSG, the EFS choice is not our problem; if it loads fs1/fs2, our
  rmtfs/modem handover differs (e.g. sector size / iovec offsets, or the modem's EFS key setup via TZ).
  Gotcha: stopping/starting the modem by hand leaves ath10k Wi-Fi dead ("cannot restart a device that hasn't been
  started") until the next boot.
- 2026-11-04 17:10: MODEM round 2 (offline + one check): Droidian's boot image carries only the generic 'SM8150 v2' DTB; the Duo board overlays are in dtbo_a (sde15; 36 overlays, Surface Duo EV1/EV1.5/EV2/EV2.1/DV A-C, no MP) - read to (local file, not published) (local only). DV_C has NO modem/RF/QLink nodes: the RF front end is the modem firmware's own business (+ RPMh votes), and downstream qcom,mss only proxies vdd_cx + vdd_mss (mainline has both power domains). No regulator is disabled by late cleanup on our boot (no 'disabling' lines) -> not an unused-regulator cut. 'RF stuck in QLINK start state' also appears on ANDROID builds of other SDM845 phones (LineageOS issues, intermittent). Our modem reports SIM 'no-atr-received' = NO SIM inserted (the tester 2026-11-04 22:10: "no sim card") - expected. Next ideas: verify on Droidian (qmicli online there: same crash = firmware/HW, not mainline); online with a SIM; EFS/rmtfs read-only (-r) vs modem's first-online writes.
- 2026-11-04 12:55: MODEM over QMI (qmi overlay: `OVERLAY_BASE="qmi-utils qrtr"`): qrtr-lookup lists the full MPSS service set (DMS, NAS, WDS, WDA, UIM, Voice, WMS, LOC v2, IPA ctl, ...); `qmicli -d qrtr://0`: revision MPSS.HE.1.0.c11-00164-SM8150_GEN_PACK (2023-05), operating mode 'shutting-down', UIM slot 1 'no-atr-received' (no SIM seen), signal none. **Do NOT --dms-set-operating-mode=online**: it crashed the modem ("rflm_qlnk_ls_retry_cnt < 2" = the RF transceiver QLink failed; recovery is off so Wi-Fi died until reset). GPS: --loc-start works, no SV info in 40 s indoors (inconclusive; may need the same RF link). Next ideas: RF/QLink supplies + GPIOs vs Microsoft's DT, eSIM/SIM detect, GNSS by a window.
