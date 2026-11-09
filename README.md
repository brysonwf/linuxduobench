# Linux Duo Bench

**Working mainline Linux 6.19 on the Microsoft Surface Duo (1st gen, SM8150).**

Upstream-style Linux kernel drivers and a Debian system layer for Microsoft's dual-screen Android phone, the Surface
Duo 1 (Qualcomm **Snapdragon 855 / SM8150**, codename "epsilon"). Both AMOLED screens, touch, the Adreno 640 GPU,
the speaker, the camera and the hinge sensor work on a mainline kernel - no Android, no downstream 4.14 vendor kernel.
**postmarketOS** (Phosh) and **Debian 13** (KDE Plasma Mobile) both ran on it, on the same kernel.

> **Temporarily not maintained** - our test phone is bricked, and we're looking for a cheap Surface Duo to continue.
> Forks and patches welcome. Open items and notes are in [`docs/`](docs/README.md).

## Drivers only
A Linux kernel patch series (device tree, DSI panel, HID-over-SPI touch, audio, camera, charger and more), the
systemd services that bring the Duo's hardware up, a Debian setup, and scripts to build a RAM boot image for the
phone's own Qualcomm bootloader (ABL, `fastboot boot` - nothing is written to the phone). **No installer.** Nothing
here writes boot partitions, the partition table or the bootloader lock state.

## Continuing the driver work: use the RAM boot image
The RAM boot image was built to **iterate fast**, and it's what to use to continue the driver work. Build a kernel,
`fastboot boot` it, test over SSH on the USB cable, power off, repeat. Each try runs from RAM, so a bad kernel costs a
reboot, never the phone. That's how every driver here was brought up. A prebuilt image is on the **Releases** page;
steps (prebuilt or your own build): [INSTALL.md](INSTALL.md).

## What works
Mainline v6.19 + 90 patches, tested on one phone:
- **Displays** - `panel-microsoft-surface-duo` (ours) on msm DSI/DPU with DSC
- **Touch** - `spi-hid-duo` (Microsoft's spi-hid, ported) + iptsd
- **GPU** - msm Adreno 640 + Mesa freedreno/Turnip
- **HDMI out (USB-C dock)** - msm DisplayPort + `qcom-pmic-typec` + FSA4480
- **Speaker** - `wcd934x` codec + `wsa881x` amp + q6 ADSP (Microsoft's tuning)
- **Microphones** - `wcd934x` DMICs
- **Camera** - `imx351` (ours) + `qcom-camss` + `dw9719` autofocus + `leds-qcom-flash`
- **Sensors (accel, compass, hinge, light/proximity)** - SLPI via `fastrpc` + hexagonrpcd + libssc
- **Wi-Fi** - `ath10k` (WCN3990)
- **Bluetooth** - `hci_qca` (WCN3990)
- **Modem / mobile data** - `qcom_q6v5_mss` + ModemManager / `ipa` v4.1
- **Charger** - `qcom_smbx`
- **Fuel gauges** - `bq27xxx` (two BQ27742)
- **Storage** - `ufs-qcom`
- **Video decode/encode** - `venus`
- **Haptics** - `da7280` (force feedback)
- **Power key** - `qcom-pon` + `pm8941-pwrkey`
- **CPU** - all 8 cores incl. the 2.84 GHz prime core; suspend (s2idle)

**postmarketOS came first.** postmarketOS v25.06 was the first full OS on this kernel: Phosh on both screens, every
driver above working, and our `duo-health` check (31 tests) passing with 0 failures. It booted both through UEFI and
straight from the phone's own bootloader. We then moved to Debian 13, for KDE Plasma Mobile's dual-screen support out
of the box and desktop apps such as VS Code. Both ran the same kernel and the same distro-neutral hardware services
(`tools/linux/system`).

On top: Debian 13 "trixie" with KDE Plasma Mobile on both screens, Waydroid for Android apps, PipeWire audio,
libcamera, and our own apps - [Hingeboard](https://github.com/brysonwf/hingeboard) (laptop mode: an on-screen keyboard
on one screen, a trackpad desktop on the other) and [Porchlight](https://github.com/brysonwf/porchlight) (an
OpenSnitch firewall UI). postmarketOS with Phosh ran on both screens too.

**GPU:** Mesa freedreno (OpenGL ES) and Turnip (Vulkan) on the Adreno 640. glmark2 at 1587x2118: terrain 74 fps,
refract 59, shadow 333, jellyfish 387, ideas 703; vkcube 59 fps. Ten minutes of the heaviest scenes held 585 MHz at
58 °C with no throttling.

**Not yet:** GPS fix, pen, calls/SMS.

**Fingerprint - partial, experimental (published unfinished):** two pending `qcom_scm` patches
(`tools/linux/kernel/pending/`) + the `duo_qsee` module (`tools/linux/drivers/duo-qsee/`). The phone's own
fingerprint app loads in TrustZone, the FPC1020 detects and captures, and a full 19-touch enrollment runs. Saving a
finger still needs a Gatekeeper-signed token - where we stopped ([notes](docs/drivers/secure-world.md)).

No firmware or Microsoft files are included; the phone's own partitions supply them at boot.

## What's where
- `tools/linux/kernel/` - patch series + `build.sh`
- `tools/linux/system/` - hardware services
- `tools/linux/debian/` - Debian setup
- `tools/linux/abl/`, `make-*.sh` - RAM boot image
- `docs/` - research notes for every driver, the decoded WCD9340/WSA8815 speaker register traces (useful for Windows
  on ARM / WOA-Project driver ports), and the Duo 1 bootloader facts

Build and boot: [INSTALL.md](INSTALL.md).

## License
GPL-2.0-or-later, except kernel code (GPL-2.0-only, like Linux) and device trees (BSD-3-Clause).
Each file says which in its `SPDX-License-Identifier` line.

## Credits
Upstream patch authors (see each patch), Microsoft's GPL spi-hid driver, the WOA-Project (gus33000 and others),
Debian, Alpine, libcamera, PipeWire, KDE Plasma Mobile. The penguin is inspired by **Tux** by **Larry Ewing**
(original art, `tools/linux/bootanim/CREDITS.md`).

## How we bricked our phone
We were packaging everything into an installer and, during experimental testing, **accidentally bricked our phone**.
The test locked the bootloader to our own signing key: it wrote `boot_a` + a signed `vbmeta_a`, flipped the slot flags,
enrolled the key and locked. The phone now boots nothing, not even Microsoft's images.

That installer is separate and **not published** - none of its code is here. (The RAM images do carry standard disk
tools like `sgdisk` and `qbootctl`; don't use them on boot partitions without a backup.)

**If you modify a Duo 1:** never relock the bootloader. There is no public recovery loader.

## Related projects
- [WOA-Project](https://github.com/WOA-Project) - Windows on ARM and UEFI for the Surface Duo
- [postmarketOS](https://postmarketos.org) and the sm8150-mainline community - mainline Linux on Qualcomm phones
- [duo-de](https://github.com/Archfx/duo-de) - a custom Android ROM for the Surface Duo

## How this was made
Engineered by Claude (Anthropic's AI); Bryson directed and tested. Bug reports: GitHub issues.
