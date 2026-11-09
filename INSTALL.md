# Building and RAM-booting Linux Duo Bench on a Surface Duo 1

This release builds a kernel and a small Debian RAM image, and starts it with `fastboot boot`. **Nothing is written to
the phone.** Power the phone off and it is exactly as before. Installing to the phone's storage is **not** part of this
release (see the warning in [README.md](README.md)).

Each step: **Do / Check / If it fails**. For an AI agent: follow the hard rules first.

## Hard rules
- **Never run `fastboot flashing lock` or `flashing lock_critical` on a modified Duo.** Never write `boot_*`,
  `vbmeta_*`, `dtbo_*` or the partition table of the boot storage unit either. That class of change is what left
  our test phone unable to boot anything.
- Run `fastboot --set-active=a` immediately before every `fastboot boot <img>`. The slot change lasts only until
  the next reboot. Without it the bootloader answers "Failed to load/authenticate boot image".
- Reset the running Linux only with `devmem 0x0c264000 w 0` (PS_HOLD). `reboot` hangs this phone.
- Never read display registers (`0x0ae0xxxx`) while the display is off. Never touch the apps SMMU (`0x15000000`).
- Charging and battery work: read `/sys/class/power_supply/bq27742-*/temp` first. Stop outside 15-35 °C.

## 0. You need
- A Surface Duo 1 whose bootloader is unlocked. `fastboot flashing unlock` erases the phone's data. Unlock only if
  you accept that, and never relock it afterwards.
- A USB-C data cable.
- A Linux machine (or Windows with WSL Ubuntu, as root) with:
  - `fastboot`
  - an aarch64 cross toolchain (`gcc-aarch64-linux-gnu`)
  - `device-tree-compiler`, `python3`, `git`, `curl`, `cpio`, `xz-utils`, `zstd`
  - a Linux git tree with the `v6.19` tag at `/root/linux` (or set `LINUX_GIT=<path>`)
- A reference boot image the phone's bootloader accepts. Its header version and addresses are copied, nothing else.
  Microsoft's own `boot.img` works: it is inside Microsoft's Surface Duo recovery package (the OTA's `payload.bin`).

## 1. Build the kernel
**Do:** `tools/linux/kernel/build.sh <this repo>`
**Check:** it ends with `KERNEL_BUILD_DONE`. `/root/build-duo` holds `arch/arm64/boot/Image.gz`,
`arch/arm64/boot/dts/qcom/sm8150-microsoft-surface-duo.dtb` and the modules.

## 2. Build the module set
**Do:** `tools/linux/make-initramfs.sh <repo> base.cpio.gz`, then
`tools/linux/initramfs-update-init.sh <repo> base.cpio.gz full.cpio.gz`.
The camera driver is optional: `tools/linux/drivers/imx351/build-oot.sh` needs the register header that
`tools/linux/camera/camx-*.py` generates from your phone's own camera module. Without it, the camera is skipped.
**Check:** `zcat full.cpio.gz | cpio -t | grep -c '^lib/duo/.*\.ko$'` lists the Duo modules.

## 3. Build the Debian RAM image
**Do:** `KEYS=<your-ssh-key>.pub MODE=boot tools/linux/debian/make-debian-rambench.sh <repo> full.cpio.gz ram.cpio.gz`
**Check:** it reports the image size (about 16 MB) and `dropbear:ok`.

## 4. Make the boot image
**Do:** `tools/linux/abl/make-abl-image.sh /root/build-duo/arch/arm64/boot/dts/qcom/sm8150-microsoft-surface-duo.dtb
/root/build-duo/arch/arm64/boot/Image.gz ram.cpio.gz <reference boot.img> duo-ram.img`
**Check:** `duo-ram.img` exists. The script refuses a kernel or ramdisk that is too big for the bootloader.

## 5. RAM-boot it (nothing on the phone changes)
**Do:** put the phone in the bootloader menu (from off: volume-down + power), then run
`fastboot --set-active=a` followed at once by `fastboot boot duo-ram.img`.
**Check:**
- both panels light up;
- a USB network appears;
- `ssh root@169.254.66.1 uname -r` answers with the kernel you built.
With no Debian on the phone, the image stays in this RAM shell: radios and DSPs are off, and SSH runs over USB.
**If it fails:** hold power to switch off. The phone is unchanged, so start again from the bootloader menu.

## Going further (developers)
`tools/linux/system/install.sh` and `tools/linux/debian/duo-debian-setup.sh` are how our Debian root was set up
(services, firewall, audio, sensors, desktop). They expect a Debian 13 arm64 root of your own. How you place that root
on the phone is up to you. Read the hard rules first: anything that writes the boot partitions is exactly what this
release leaves out.
