# Linux Duo Bench

**Working mainline Linux 6.19 on the Microsoft Surface Duo (1st gen, SM8150).**

### postmarketOS and Debian both run on it, on both screens.

postmarketOS (Phosh) first, then Debian 13 (KDE Plasma Mobile), on a mainline kernel - no Android, no vendor kernel.
Qualcomm Snapdragon 855 / SM8150, codename "epsilon".

> **Temporarily not maintained** - our test phone is bricked; we're looking for a cheap Surface Duo to continue.
> Forks and patches welcome. Open items and notes: [`docs/`](docs/README.md).

## Drivers only
Kernel patch series (device tree, DSI panel, HID-over-SPI touch, audio, camera, charger...), systemd hardware
services, a Debian setup, and a RAM boot image for the phone's Qualcomm bootloader (ABL, `fastboot boot`).
**No installer** - nothing here writes boot partitions, the partition table or the lock state.

## Continuing the driver work: use the RAM boot image
Built to **iterate fast**: build a kernel, `fastboot boot` it, test over SSH on the USB cable, power off, repeat. A bad
kernel costs a reboot, never the phone. Prebuilt image: **Releases** page. Steps: [INSTALL.md](INSTALL.md).

## What works (mainline v6.19 + 90 patches, one phone)
- **Displays** - `panel-microsoft-surface-duo` (ours), msm DSI/DPU with DSC
- **Touch** - `spi-hid-duo` (Microsoft's spi-hid, ported) + iptsd
- **GPU** - msm Adreno 640 + Mesa freedreno (OpenGL ES) / Turnip (Vulkan)
- **HDMI out (USB-C dock)** - msm DisplayPort + `qcom-pmic-typec` + FSA4480
- **Speaker** - `wcd934x` codec + `wsa881x` amp + q6 ADSP (Microsoft's tuning)
- **Microphones** - `wcd934x` DMICs
- **Camera** - `imx351` (ours) + `qcom-camss` + `dw9719` autofocus + `leds-qcom-flash`
- **Sensors (accel, compass, hinge, light/proximity)** - SLPI via `fastrpc` + hexagonrpcd + libssc
- **Wi-Fi / Bluetooth** - `ath10k` / `hci_qca` (WCN3990)
- **Modem / mobile data** - `qcom_q6v5_mss` + ModemManager / `ipa` v4.1
- **Charger / fuel gauges** - `qcom_smbx` / `bq27xxx` (two BQ27742)
- **Storage** - `ufs-qcom`
- **Video decode/encode** - `venus`
- **Haptics** - `da7280` (force feedback)
- **Power key** - `qcom-pon` + `pm8941-pwrkey`
- **CPU** - all 8 cores incl. the 2.84 GHz prime core; suspend (s2idle)

**Not yet:** GPS fix, pen, calls/SMS.
**Fingerprint (partial, experimental):** pending `qcom_scm` patches + the `duo_qsee` module - the FPC1020 captures and a
full enrollment runs in TrustZone; saving a finger needs a Gatekeeper token ([notes](docs/drivers/secure-world.md)).

No firmware or Microsoft files included; the phone's own partitions supply them at boot.

## postmarketOS, then Debian
- **postmarketOS v25.06 came first.** We added the Duo's second screen to Phosh (both panels in phoc, each touchscreen
  on its own panel; the shell bar stays on one). Every driver worked; `duo-health` (31 tests) passed with 0 failures.
- **Debian 13 "trixie" next**, for KDE Plasma Mobile's dual-screen support and desktop apps like VS Code. Also:
  Waydroid (Android apps), PipeWire, libcamera, [Hingeboard](https://github.com/brysonwf/hingeboard) (laptop mode:
  keyboard on one screen, trackpad desktop on the other), [Porchlight](https://github.com/brysonwf/porchlight)
  (OpenSnitch firewall UI).
- Same kernel and hardware services (`tools/linux/system`) for both.

**GPU benchmarks (glmark2, 1587x2118):** terrain 74 fps, refract 59, shadow 333, jellyfish 387, ideas 703; vkcube 59
fps. Ten minutes of the heaviest scenes held 585 MHz at 58 °C, no throttling.

## What's where
- `tools/linux/kernel/` - patch series + `build.sh`
- `tools/linux/system/` - hardware services
- `tools/linux/debian/` - Debian setup
- `tools/linux/abl/`, `make-*.sh` - RAM boot image
- `docs/` - driver notes, decoded WCD9340/WSA8815 speaker traces (for Windows on ARM / WOA ports), bootloader facts

## License
GPL-2.0-or-later, except kernel code (GPL-2.0-only, like Linux) and device trees (BSD-3-Clause). See each file's
`SPDX-License-Identifier`.

## Credits
Upstream patch authors (see each patch), Microsoft's GPL spi-hid driver, the WOA-Project (gus33000 and others),
Debian, Alpine, libcamera, PipeWire, KDE Plasma Mobile. The penguin is inspired by **Tux** by **Larry Ewing**
(original art, `tools/linux/bootanim/CREDITS.md`).

## How we bricked our phone
While packaging everything into an installer, experimental testing **accidentally bricked our phone**: the test wrote
`boot_a` + a self-signed `vbmeta_a`, flipped the slot flags, enrolled our key and locked the bootloader. It now boots
nothing, not even Microsoft's images. That installer is **not published** - none of its code is here. (The RAM images
carry standard disk tools like `sgdisk` and `qbootctl`; don't use them on boot partitions without a backup.)
Details: [docs/brick-record.md](docs/brick-record.md). **Modifying a Duo 1? Never relock the bootloader** - there is no
public recovery loader.

## Related projects
[WOA-Project](https://github.com/WOA-Project) (Windows on ARM, UEFI) ·
[postmarketOS](https://postmarketos.org) / sm8150-mainline · [duo-de](https://github.com/Archfx/duo-de) (Android ROM)

Engineered by Claude (Anthropic's AI); Bryson directed and tested. Bug reports: GitHub issues.
