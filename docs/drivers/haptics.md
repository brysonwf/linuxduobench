# Workbench — Haptics (DA7280)

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)). Text moved unchanged from leg 33 on 2026-11-05; newest log entries first.

#### Batteries + haptics (2026-11-02)
Upstream's Duo DT already has both BQ27742 fuel gauges (i2c1 + i2c17 @0x55) and the DA7280 haptics (i2c1 @0x4a);
they were missing only because `I2C_QCOM_GENI` and `INPUT_DA7280_HAPTICS` were modules (not shipped). Built in
(kernel #16): `/sys/class/power_supply/bq27742-0` 60 % and `-1` 63 %, 3.92 V, "Charging"; `da7280-haptic` = event0,
driven by `tools/linux/buzz` (FF_CONSTANT). Input devices now: da7280-haptic, gpio-keys (volume), headset jack.


## Log
- 2026-11-08 17:15: **HAPTICS WORK ON DEBIAN/PLASMA (kernel 0089).** Root cause of all three "haptics freezes": runtime
  `rmmod da7280` + `insmod` - the swap oopses (paging fault in the old module's memory 17 ms after the new device
  registers) and freezes the SoC; earlier "patched module" tests were even the STOCK module (an `M=` build writes the
  .ko into the source tree, the copy came from O=). Fix path: the FF_RUMBLE patch as series 0089, booted (ABL image,
  no swap): direct FF_RUMBLE 400 ms plays (the tester felt it), no errors; hfd-service (Plasma Mobile) with duo-hw ordered
  before it + accountsservice installed: "Using force feedback vibrator", the D-Bus `Vibrator.rumble(300, 1)` call
  succeeds. Rules: never swap da7280 at runtime; test motor changes as a booted kernel.
- 2026-11-08 16:10-16:18: **Debian/Plasma (hfd-service) + the motor - third freeze.** Findings, in order:
  - hfd-service (Plasma Mobile's haptics, 0.2.3) picked its vibrator ONCE at start (10 s) - before duo-hw loaded the
    da7280 module (21 s) -> "Using dummy vibrator". Fix: duo-hw.service `Before=hfd-service.service`; hfd then finds
    the motor but "Failed to upload rumble effect" with the STOCK module (kernel: "Device can only accept FF_CUSTOM
    waveform" = the input core's rumble->sine emulation, rejected) - no freeze.
  - With the pending FF_RUMBLE patch as a module (built against 00088 via M=, vermagic match): hfd says "Using force
    feedback vibrator" but the upload STILL fails with the same kernel message -> hfd uploads something other than
    FF_RUMBLE (periodic?) or goes through the emulation anyway. Not understood yet.
  - Restarting hfd under strace (to read its EVIOCSFF effect type) while apt installed strace -> **hard freeze**: no
    SSH, no fastboot, both panels dark, the watchdog did not reset it (power button needed, 16:18). Last kmsg line =
    the FF_CUSTOM rejection; nothing new. Third freeze around a daemon taking the motor (feedbackd twice on pmOS).
  - Next (with the tester holding the phone, kmsg streamed, nothing else running): read hfd's vibrator-ff source for the
    exact effect + gain/play sequence, replay it from a script one ioctl at a time (gain, upload, play, stop) to find
    the call that freezes; until then keep hfd away from the motor at boot (do not ship the duo-hw ordering).
- 2026-11-07 23:35-23:40: **FF_RUMBLE works; feedbackd use froze the phone AGAIN.** The pending rumble patch as a module
  for the running kernel (6.19.0-00072; build gotcha: `make O=/root/build-duo M=drivers/input/misc` builds IN THE
  SOURCE TREE - take the .ko from there, clean it out after), insmod on pmOS: ff caps 80/81/82/93/96, a direct FF_RUMBLE
  upload (python-evdev, strong 0xC000, 400 ms) plays - the tester felt it, twice, no kernel errors. (Without the patch the
  input core turns the rumble into a periodic sine, which the driver rejects: "Device can only accept FF_CUSTOM".)
  Then: udev rule tagging the motor FEEDBACKD_TYPE=vibra + `udevadm trigger -s input` + feedbackd restart -> the SSH
  command hung, the phone froze, the watchdog reset it to fastboot ~1 min later (the tester had opened Camera at the same
  moment; last kmsg line a garbled `da7280-haptic:` warning; the saved ramoops is bit-decayed). Second freeze at the
  moment feedbackd takes the motor (first: 14:15) - feedbackd's way of driving it (effect sequences, FF_GAIN, stop/start
  overlap; maybe the DA7280 SEQ_FAULT IRQ path) is the suspect, not FF_RUMBLE itself. The rule is NOT installed
  (it never reached the disk). Next, with the tester nearby and kmsg streamed: replay feedbackd's exact sequence from a
  script (read fbd-dev-vibra.c: gain write, upload, play, repeated play/stop) step by step to find the freezing call.
- 2026-11-07 night: **FF_RUMBLE patch builds on the current series and the motor advertises it** (test kernel
  6.19.0-rumble-00068, RAM workbench one-shot): da7280-haptic ff caps `120070000 0` = FF_RUMBLE (80) + PERIODIC (81)
  + CONSTANT (82) + CUSTOM (93) + GAIN (96). Read-only check, nothing played. The vibration test (feedbackd +
  the parked udev rule, `fbcli -E message-new-instant`, then a tap in Phosh) stays a WITH-the tester item (the 14:15 freeze
  needed the power button). Test files: (local file, not published)
- 2026-11-07 15:30: **Why Phosh cannot vibrate the Duo: feedbackd needs FF_RUMBLE.** The installed feedbackd's strings
  ("No rumble capable vibra device", "Failed to upload rumbling vibra effect", vibra-rumble / -periodic / -pattern
  sources) show it drives rumble (+ periodic) effects; the mainline da7280 driver offers only FF_CONSTANT and
  FF_PERIODIC with FF_CUSTOM waveforms. So the udev tag alone could never have worked - and the 14:15 freeze most
  likely did NOT come from the motor (feedbackd refuses the device or its uploads fail before any I2C write; the
  phone has had unrelated random freezes). Fix written: "Input: da7280 - support FF_RUMBLE" (rumble = constant effect
  at the stronger magnitude), compile-tested, NOT phone-tested: side branch duo/da7280-rumble (WSL /root/linux-rumble)
  + `tools/linux/kernel/pending/`. Test with the tester nearby: the da7280 module from that branch (module only - but
  vermagic: build it against the running release) + the parked udev rule -> `fbcli -E message-new-instant` buzzes,
  then a tap in Phosh. Bonus objective check: the accelerometer's noise jumps while the motor runs.
- 2026-11-07 14:15: **Phosh never vibrates: feedbackd does not know the DA7280 - first fix attempt coincided with a
  hard freeze (OPEN).** feedbackd's 90-feedbackd.rules tags vibrators by input-device NAME (aw8695-haptics,
  drv260x:haptics, pm8xxx_vib_ffmemless, ...); ours is `da7280-haptic` (FF caps 120070000: CONSTANT, PERIODIC,
  CUSTOM, GAIN - no FF_RUMBLE), so it has only ID_INPUT=1 and feedbackd ignores it. Rule
  `tools/linux/haptics/91-duo-feedbackd-da7280.rules` (same form) tagged it FEEDBACKD_TYPE=vibra; feedbackd restarted by
  D-Bus activation on `fbcli -E message-new-instant` -> the SSH command hung and the WHOLE phone froze (no SSH on USB or
  Wi-Fi, USB still enumerated, hardware watchdog did not bite -> the tester's power button). The rule file was 0 bytes
  afterwards (written seconds before the freeze). pstore of that boot not kept (power button, not PS_HOLD).
  Not proven: da7280_haptics_upload_effect() accepts FF_CONSTANT and FF_PERIODIC only with FF_CUSTOM, so feedbackd's
  usual FF_RUMBLE / FF_SINE uploads should fail with -EINVAL and never reach the chip - the freeze may be a coincidence.
  The rule is NOT installed. Controlled re-test (the tester nearby): RAM workbench, kmsg streamed to the PC, watch the
  da7280 IRQ count in /proc/interrupts, then (1) an fftest-style FF_RUMBLE upload, (2) FF_PERIODIC FF_SINE, (3) the
  exact effect feedbackd 0.x sends (read fbd-dev-vibra.c), one at a time. If the driver is innocent, retry feedbackd on
  pmOS with its log on (G_MESSAGES_DEBUG=all). If feedbackd needs FF_RUMBLE, the driver may need a rumble mapping.

- 2026-11-04 15:40: **HAPTICS WORK** (the tester: "it buzzed" on the 1 s @32000 buzz). Made default: init loads /lib/duo/da7280.ko when the DT node is okay (duo.nohaptics=1 off), initramfs-update-init.sh copies the module + builds buzz into /usr/local/bin. One-shot of the full set (kernel #64 + initramfs #75 + DTB 55-haptics): driver loaded at 3.3 s, buzz at 120 s uptime, 0 stalls -> installed as the DEFAULT + known-good (previous set in known-good-prev). Headset jack: the Duo has NO 3.5 mm jack (USB-C only) -> the wcd934x MBHC 'headset' input is unwired; dropped from the driver list.
- 2026-11-04 11:30: Haptics one-shot (DA7280 status okay on DTB #51, TEST entry with the display fix) HARD-HUNG again → it really is the DA7280 path (the tester power button). mem-array is identical to Microsoft's (100 values); differences left: Microsoft resonant 190 Hz (ours 180) and IRQ type 0/default (ours IRQ_TYPE_LEVEL_LOW, a stuck-low SEQ_FAULT line + threaded ONESHOT handler = the earlier "reload PS_SEQ_ID & mem data" + RCU stall). PARKED; next try: IRQ_TYPE_EDGE_FALLING + 190 Hz. TE works (pp0 te_rd_ptr irq ~32/s).
