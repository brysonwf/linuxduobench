# Linux Duo Bench

Mainline Linux drivers for the **Microsoft Surface Duo (1st gen, SM8150)**.

> **Not actively maintained.** Forks and patches welcome. Open items and notes are in [`docs/`](docs/README.md).

## Drivers only
Kernel patches, hardware services, Debian setup, and scripts to build a RAM boot image (`fastboot boot` - nothing is
written to the phone). **No installer.** Nothing here writes boot partitions, the partition table or the lock state.

## What works
Mainline v6.19 + 90 patches, tested on one phone:
both screens + touch, GPU, HDMI via dock, speaker (Microsoft's own DSP tuning) + mics, camera (autofocus, flash),
sensors incl. hinge angle, Wi-Fi, Bluetooth, modem, charger, suspend, all 8 cores, video decode/encode, haptics.
Debian 13 + Plasma Mobile runs on top.

**Not yet:** GPS fix, pen, calls/SMS, fingerprint.

No firmware or Microsoft files are included; the phone's own partitions supply them at boot.

## What's where
- `tools/linux/kernel/` - patch series + `build.sh`
- `tools/linux/system/` - hardware services
- `tools/linux/debian/` - Debian setup
- `tools/linux/abl/`, `make-*.sh` - RAM boot image
- `docs/` - research notes, incl. decoded speaker traces for Windows ports

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

## How this was made
Engineered by Claude (Anthropic's AI); Bryson directed and tested. Bug reports: GitHub issues.
