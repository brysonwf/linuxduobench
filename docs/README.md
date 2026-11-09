# Research notes

These are the lab notebooks behind each driver: what the hardware is, how Android or Microsoft drives it, what we
tried, what failed and what works. They are working notes, so expect:
- dated, newest-first entries;
- kernel and initramfs build numbers from our own test loop;
- references to files that are not published (local captures, the installer, other chapters).
They are published because they are useful to anyone bringing up the Surface Duo 1 on another OS.

Licensed like the userspace: **GPL-2.0-or-later**. No firmware, tuning data or vendor files are included. Where a note
quotes a value from the phone's own configuration (DT, mixer paths, thermal limits), that value is a hardware fact
needed to drive the part.

## For a Windows driver port (WOA-Project and others)
The Linux drivers here are GPL, so do not copy their code into a closed driver. The notes describe the hardware and
are meant as a specification.
- **Speaker (the open item on Windows):** start with `drivers/audio-mics.md` and `captures/audio/`.
  - `wsa8815-init-sequence.txt` lists the 61 register writes the WSA8815 amplifier gets at boot, through the
    WCD9340's SoundWire master.
  - The decoded `regwrite-trace-*` files show codec init and stream start/stop (`WSA881X_SPKR_DRV_EN`
    0x7c <-> 0xfc, the RX INT7 path, BOOST).
  - `regdump-diff-idle-vs-playing.txt` shows what changes while sound plays.
  - `windows/24-audio.md` is our Windows-side investigation of qcauddev8150: endpoints with `SlaveInfo=0`,
    SoundWire/WSA code paths, IOCTL map.
  - `drivers/adsp-calibration.md` shows how Microsoft's own speaker tuning (ACDB topologies 0x10010313 / AFE
    0x1001025E, mono 24-bit) is loaded on the ADSP.
- **Prime core:** `windows/19-primecore.md` covers Windows. On Linux all 8 cores run, including the 2.84 GHz prime core
  (`drivers/thermal.md`, `drivers/charger-power.md`).
- **Sensors (hinge angle, posture, light/proximity):** `drivers/sensors.md` covers the SLPI/SSC path.
  `windows/23-sensors-interface.md` covers the Windows sensor stack.
- **Cameras:** `drivers/camera.md` describes the raw CAMSS + IMX351 bring-up with no secure firmware: autofocus
  (DW9800), flash LED, orientation. `windows/28-cameras.md` covers Windows.
- **Display and touch:** `drivers/display.md` covers the DSC command-mode panel details: burst mode off, 2 slices per
  packet, re-sending brightness on enable, and the first-init FIFO error. `drivers/touch-pen.md` and
  `windows/22-input-touch.md` cover touch.
- **HDMI / DisplayPort out:** `drivers/hdmi-usbc.md` covers the USB-C dock, DP alt mode and the PHY orientation.
- **Modem and mobile data:** `drivers/modem.md` (low-power before online, the QLink crash), `drivers/ipa-v4.1.md` and
  `windows/26-cellular-modem.md`.
- **Fingerprint:** `drivers/secure-world.md` covers QSEECOM on mainline: loading the TrustZone app, the listeners and
  the enrollment flow, with the scope we kept (no key extraction, no bypass).

## Index
| Folder | Contents |
|---|---|
| `drivers/` | one chapter per driver on mainline Linux: audio, ADSP calibration, display, GPU, touch/pen, camera, video codec, sensors, charger/power, thermal, haptics, HDMI/USB-C, modem, IPA, Wi-Fi/BT, UFS, secure world, kernel layout, Android parity |
| `windows/` | the Windows driver research most useful to other projects (prime core, touch, sensors, audio, modem, cameras) |
| `captures/audio/` | decoded SoundWire/codec register traces from the working Linux speaker path |
