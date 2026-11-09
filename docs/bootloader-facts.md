# Surface Duo 1 bootloader: what we observed

Facts from our phone (bootloader 2022.815.527, "Customer mode", unlocked). Read before you touch one.

## Booting your own image
- `fastboot boot <img>` (RAM only) works **only right after `fastboot --set-active=a`** in the same session; on slot b
  it answers "Failed to load/authenticate boot image". The slot change does not survive a reboot.
- ABL accepts a header-v2 boot image; kernel under ~60 MiB decompressed, ramdisk well under ~40 MB (19 MB boots,
  44-52 MB were refused). `tools/linux/abl/make-abl-image.sh` handles the layout and the DTB.
- The kernel learns the lock state from `androidboot.verifiedbootstate` (orange = unlocked).

## What fastboot cannot do
- `fastboot flash` / `erase` of `boot_*`, `vbmeta_*`, `dtbo_*` -> "Device Error": that storage unit (the 4th UFS LUN)
  is read-only to the bootloader; only a running Linux can write it.
- `--set-active` and the slot flags do not persist from fastboot (they live on that same unit).
- In Customer mode `oem set-successful`, `oem clear-devinforollback` and almost every other `oem` command are refused;
  `oem device-info` works.
- Erases are refused when the battery is very low ("battery's combined rsoc is very low").

## What works
- `fastboot flashing unlock` (with OEM unlocking on in Android) - erases userdata.
- Restarting into fastboot from Linux: write `bootonce-bootloader` into the `misc` partition's bootloader message.
- Erasing `misc`, `metadata`, `userdata`, `vbmeta_system` from fastboot.

## Recovery options that do NOT exist publicly
- No Microsoft-signed EDL (9008) firehose loader is public; every public loader is rejected (PK_HASH mismatch).
- Microsoft's recovery package (adb sideload from Android Recovery) only helps while Android Recovery still boots.

## Never
- Never `fastboot flashing lock` / `lock_critical` a modified Duo, never enroll `avb_custom_key`, never write
  boot/vbmeta/dtbo with your own keys. That is how ours was bricked: [brick-record.md](brick-record.md).
- Switch slots only the way Android does (qbootctl: every A/B partition + the UFS boot LUN), never just boot_a/boot_b.
