# Linux Duo Bench

> **Not actively maintained.** Forks and patches are welcome. Open items and everything we learned are in
> [`docs/`](docs/README.md).

Mainline Linux drivers and a Debian system layer for the **Microsoft Surface Duo (1st gen, SM8150)**.

## The drivers are not the installer

This repository is **drivers and userspace only**:
- the kernel patch series;
- the services that bring the hardware up;
- the Debian setup;
- the scripts that build a boot image you start from RAM with `fastboot boot`, which changes nothing on the phone.

**The installer is a separate program and is not published.** The brick happened during an experiment outside the
scope of this release: we were testing whether the phone would boot our own system with the bootloader locked to our
own signing key. That experiment bricked our only stock test phone. Our installer
wrote `boot_a` and a `vbmeta_a` signed with our own key, flipped the A/B slot flags with `sgdisk`, enrolled that key
(`avb_custom_key`) and locked the bootloader. After that the phone boots no image at all, not even Microsoft's own
(fastboot only). We know which steps did it, not yet which internal state they broke.

**None of that code is here.** There is no installer, no AVB signing, no lock or unlock step and no slot-switching
script. No code in this repository writes the boot, vbmeta or dtbo partitions, a partition table or the bootloader's
lock state, and the drivers and services never touch them. Two caveats:
- The RAM images include standard disk tools (`sgdisk`, `qbootctl`) for developers to use by hand. Don't point them at
  boot partitions or slot flags without a backup.
- The Debian setup installs Debian's `qbootctl` package. Its service marks the running slot "successful" at boot, as
  Android does on every boot.

**Status (2026-11):** every driver below works on one phone (ours). Nobody else has run this yet.

## What works (mainline v6.19 + this series)
- **Display and input:** both panels (DSC command mode), touch on both screens (HID-over-SPI + iptsd multitouch), and
  HDMI out through a USB-C dock. The GPU (Adreno 640) works too.
- **Audio:** the speaker (WCD9340 + WSA8815, with the phone's own DSP tuning generated on the phone) and the microphones.
- **Connectivity:** Wi-Fi, Bluetooth (the phone's own address), and the modem in low-power and online modes. The IPA
  mobile-data path works.
- **Sensors:** accelerometer, compass, hinge angle, and light/proximity. They are read through the sensor DSP
  (hexagonrpcd + libssc).
- **Camera:** IMX351 with autofocus and flash/torch. Its register tables are generated on the phone from its own
  camera module.
- **Video:** decode and encode (Venus).
- **Power:** the charger (with temperature limits) and both fuel gauges. All 8 CPU cores run, including the 2.84 GHz
  prime core, and suspend (s2idle) works.
- **Other:** the haptic motor (force feedback) and the power key.
- **Debian setup:** a Debian 13 setup (Plasma Mobile, and our own Hingeboard for laptop mode) runs on top of this.
  The setup scripts are in `tools/linux/debian/`.

Known gaps:
- GPS gets no satellite fix yet.
- The pen is untested.
- Calls and SMS are untested.
- Fingerprint is not supported: its sensor is owned by TrustZone.

## What is here
- `tools/linux/kernel/`: mainline **v6.19** plus a `git am` patch series (upstream series we carry plus our own
  commits) and `build.sh`. The kernel config is part of the series (`make defconfig duo.config`).
- `tools/linux/abl/`, `make-initramfs.sh`, `initramfs-update-init.sh`, `debian/make-debian-rambench.sh`: these build
  a boot image the phone's own bootloader starts with `fastboot boot`.
- `tools/linux/system/`: the systemd services and `/usr/libexec/duo` scripts that bring the hardware up.
- `tools/linux/debian/`: the Debian setup and the config files it installs.
- `tools/linux/drivers/imx351`, `camera/`, `audio-tuning/`, `modem/`: the camera driver, the DSP tuning generator and
  the GPS helper.
- `tools/linux/bootanim/`, `splash/`: the boot animation and logo, drawn in code (see the credits).

**No firmware or vendor data is included.** All DSP firmware and the Wi-Fi board data are read at boot from the
phone's own `modem_a` partition. Speaker tuning and camera tables are generated on the phone from its own partitions.
Hardware parameters (panel init, charger and thermal limits) were derived from the phone's own configuration. No
Microsoft files are included.

## Research notes
[`docs/`](docs/README.md) holds the lab notebook behind every driver: how the hardware works, what Android and
Microsoft do, and what failed and what worked. It also has the decoded speaker register traces, and the Windows-side
research most useful to a Windows driver port (WOA-Project), e.g. the WCD9340/WSA8815 speaker path.

## License
- **Everything we wrote outside the kernel** (scripts, services, tools, docs): **GPL-2.0-or-later**, so GPL-3
  projects can use it.
- **Kernel code** (the patches in `tools/linux/kernel/series`, and the camera driver): **GPL-2.0-only**, because it
  has to be. Most patches edit files that are GPL-2.0-only in Linux. Others are third-party patches whose authors
  chose GPL-2.0-only. Our own new drivers (panel, IMX351 camera, IPA v4.1 data) follow or copy GPL-2.0-only drivers.
  Upstream patches keep their authors and headers.
- **Device-tree files** we added: **BSD-3-Clause** (the kernel's usual choice for DT, and usable by GPL-2.0-or-later
  and GPL-3 projects).
- Each file names its license in its `SPDX-License-Identifier` line. [LICENSE](LICENSE) holds the GPL-2.0 text.

## Using this in another project
Other Surface Duo projects are welcome to take any part of this:
- **The kernel series:** every change is one commit with its own explanation, so a single driver or fix can be
  picked out.
- **The userspace:** plain shell, Python and C, with no build system of its own.

## Reporting problems
Please use GitHub issues. Include `uname -r`, what you ran, and the output.

## Credits
- Linux and the upstream series in `kernel/series` keep their authors (see each patch). They include:
  - HID-over-SPI (Microsoft / upstream spi-hid)
  - WCN39xx power sequencing (Dmitry Baryshkov)
  - SMB5 charger (Casey Connolly)
  - fastrpc, ath10k and others
- The Duo digitizer driver is a port of Microsoft's GPL spi-hid driver from their Surface Duo kernel.
- The WOA-Project (gus33000 and others), for years of Surface Duo research.
- Debian, Alpine Linux, libcamera, PipeWire, KDE Plasma Mobile.
- The Linux Duo Bench penguin is inspired by **Tux**, created by **Larry Ewing** with The GIMP. The art here is an
  original drawing made in code (see `tools/linux/bootanim/CREDITS.md`).

## How this was made
Engineered by Claude (Anthropic's AI), working on the phone end to end, with Bryson directing and testing.
