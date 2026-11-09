# Brick record: how our Surface Duo 1 stopped booting

**Summary.** We were packaging everything into an installer and tested whether the phone would boot our own system
with the bootloader locked to our own signing key. The run wrote `boot_a` + a self-signed `vbmeta_a`, flipped only the
boot slot flags (sgdisk), enrolled the key (`avb_custom_key`) and locked. The locked boot was refused; after unlocking
again, **no image boots on either slot** - not even Microsoft's own - with "Device Error". Our own plan below said to
test the key on an unlocked phone first; the run skipped that. What we tried afterwards, the findings ("Device Error"
is not a verification failure; the mixed slot state) and what is left are recorded below. Short version for anyone
else: [bootloader-facts.md](bootloader-facts.md). The original lab notes follow unchanged (apart from names).

---

# Workbench — a locked bootloader with OUR key (verified boot)

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)).

the tester 2026-11-08: "Can we write our own locked bootloader?" / "Can we install pixels bootloader so they can add a
custom key?"

## Answer
- **A Pixel bootloader: no.** The boot ROM checks the first-stage loader (XBL) against a key hash fused into the SoC
  (Microsoft's); XBL checks ABL (Microsoft-signed). A Google-signed Pixel ABL - built for other hardware anyway -
  would be refused and the phone would not reach fastboot again (EDL rescue needs a Microsoft-signed firehose).
  `fastboot getvar all` (read-only, 2026-11-08): `secure: yes`, `unlocked: yes`, bootloader 2022.815.527.
- **But Microsoft's own ABL already has the Pixel feature.** The ABL on abl_a (an ELF wrapping a UEFI firmware
  volume, one LZMA section at 0x3078 -> 565448 bytes; decompressed with a small python lzma scan, the image stays
  local in (local file, not published)) contains Qualcomm's user-key verified boot:
  `flashing avb_custom_key`, `erasing avb_custom_key`, StoreUserKey / GetUserKey / EraseUserKey,
  `ValidateVbmetaPublicKey OutIsTrusted %d, UserKey %d`, `ValidatePartitionPublicKey ...`, `flashing unlock`,
  `flashing get_unlock_ability`, `is_unlock_critical` - the same AVB "user-settable root of trust" Pixels use.

## What a locked Duo with our key would look like
1. Keep the bootloader unlocked while building: an AVB key pair of our own (avbtool), our kernel + initramfs as an
   Android boot image (tools/linux/abl/make-abl-image.sh - the no-UEFI ABL path already boots both panels, touch,
   sound, UFS), a vbmeta signed with our key covering boot (+ dtbo).
2. `fastboot flash avb_custom_key <our public key in AVB format>`; flash boot_a + vbmeta_a.
3. `fastboot flashing lock` (wipes userdata) -> the phone boots ONLY our signed images; ABL shows the yellow "custom
   key" warning like a Pixel with a custom OS. Disk encryption with a boot password completes the chain.
Consequences: a locked bootloader refuses `fastboot boot` and flashing - the RAM-boot workflow (TOP RULE: every boot
stops at fastboot, the PC RAM-boots what it needs) ends; updates must be signed images written from the running OS
(A/B slots). Unlocking again (`flashing unlock`) wipes userdata and needs get_unlock_ability = 1.

## What the stock vbmeta_a verifies (2026-11-08, read-only copy, tools/linux/abl/vbmeta-info.py)
libavb 1.0, **SHA256_RSA4096**, rollback index 0, flags 0, made by avbtool 1.2.0:
- `hash` **boot** (51187712 bytes, sha256) and `hash` **dtbo** (5231866 bytes, sha256) - whole-image digests
- `hashtree` **vendor** + **odm** (dm-verity, sha1) - inside `super`
- `chain` **vbmeta_system** (its own 4096-bit key, rollback location 2) - system/product
-> Our vbmeta for a Debian-only phone: `hash` boot (our ABL image: kernel + stage 1 + DTB) + `hash` dtbo (the phone's
own overlay, which ABL applies before handing over), signed RSA4096 with our key; no vendor/odm/system entries (super
is gone). Making it needs avbtool (Google's AOSP `external/avb/avbtool.py`; Ubuntu 22.04 and Debian trixie do not
package it) - fetching it was blocked for Claude by the auto-mode classifier ("code from external"): the tester runs the
fetch (job script avb-fetch.sh) or a permission rule allows it.

## Before trying (read-only checks first)
- `fastboot flashing get_unlock_ability` (read-only): **1** (checked 2026-11-08) - unlocking again stays possible
  if a signed image ever fails to boot (with a userdata wipe), so locking is recoverable.
- Which partitions the stock vbmeta chains (boot, dtbo, vendor_boot?, system/vendor hashtrees) - from the phone's own
  vbmeta_a (avbtool info_image, read-only copy).
- A test on the UNLOCKED phone first: flash our key + signed images, boot them (ABL then reports whether they verify
  with the user key: "ValidateVbmetaPublicKey OutIsTrusted ..., UserKey 1") - only then lock.
Decision = the tester's (it ends the RAM-boot model and wipes userdata).

## 2026-11-08 ~22:10 - LOCKED with our key on the STOCK phone: the bootloader refuses to boot it
Installer run on the stock phone: boot_a = our image (keyed), vbmeta_a = boot + dtbo hash descriptors signed with the
user's RSA-4096 key (AvbSign.ps1), `fastboot flash avb_custom_key` OKAY, `fastboot flashing lock` confirmed on the
phone -> unlocked: no, current-slot a. Then every boot of slot a: back to Boot Select at once; `fastboot continue` ->
**"Failed to load image from partition: Device Error"** (= LoadImageAndAuthVB2 failed). slot-retry-count:a stays 6
(verification failures do NOT count as boot attempts -> the 7-try fallback to slot b never triggers for this case).
Recovered with `fastboot flashing unlock` (unlock-ability 1; erases userdata, empty).
- Our images are valid: Google's avbtool `verify_image` passes the vbmeta signature and the boot + dtbo hashes, and the
  vbmeta's key sha1 = avb_pkmd.bin's (39620ffd...).
- The ABL requests only boot + dtbo (+ vendor_boot if valid; strings: "Requested Partition: %a"), = our descriptors.
- Remaining candidates (ABL strings): "LoadImageAndAuthVB2: Error int TZ Rollback Version syscall" (rollback index /
  TZ step for the user-key root of trust), or the user-key check refused in Microsoft's "Customer mode" (`fastboot oem
  show-devinfo` -> "Can't access in Customer mode"). No log readable from fastboot (no oem log command).
- **Discriminating test (next):** Microsoft's own boot + dtbo + vbmeta descriptors (chain vbmeta_system, hashtrees
  vendor/odm) re-signed with OUR key (avbtool make_vbmeta_image --include_descriptors_from_image), Microsoft's boot
  image on boot_a, key enrolled, lock -> does stock Android boot in yellow? Yes = user keys work, something in our
  image/vbmeta is refused; no = the retail ABL does not honour user keys -> a locked Debian Duo is impossible and the
  installer's default must be unlocked.

## 2026-11-08 ~22:20-22:45 - AFTER the locked attempt: NO slot boots any more (open, phone in fastboot)
After unlocking again (`flashing unlock` OK, unlocked: yes), EVERY boot fails, both slots, also Microsoft's untouched
slot b, also after a COLD power cycle (power held 15 s, then volume-down + power):
- `fastboot --set-active=b` + `continue` -> "Failed to load image from partition: Device Error"
- `fastboot --set-active=a` + `boot <our RAM image>` (worked earlier tonight) -> "Failed to load/authenticate boot
  image: Device Error"
- `fastboot erase avb_custom_key` OK (no change); `fastboot flash vbmeta_a <Microsoft's>` -> "Error flashing partition:
  Device Error" (fastboot cannot write that storage unit at all on this phone); `oem clear-devinforollback` and
  `oem show-devinfo` -> "Can't access in Customer mode".
- GPT still readable (partition sizes, slot flags fine; slot-successful:b became "no", retries 7/7).
- What changed persistently just before: Linux wrote boot_a + vbmeta_a (ours), `duo-slot arm a` rewrote the boot
  unit's GPT attributes with sgdisk (never tested with a real reboot before), avb_custom_key enrolled, locked, ONE
  locked boot attempt of a user-key image, unlocked.
- ABL strings point at TrustZone-side failures that persist: `KeyMasterStartApp: QseecomStartApp failed`,
  `KeyMasterSendRotAndBootState: Set ROT err`, `SetFuse (TZ_HLOS_IMG_TAMPER_FUSE) fails!`,
  `TZ_UPDATE_ROLLBACK_VERSION_ID failed` - any of them aborts LoadImageAndAuth with a generic error. Not separable
  without the bootloader log (none readable via fastboot; no `oem` log command).
- LESSON for the installer (before ANY further stock-phone run): no slot/GPT edits on the boot storage unit, no lock
  path, until this is understood; the lock feature is suspended.
- 22:50: Recovery Mode (slot b selected in-session), RAM boots of the Debian bench image AND TWRP: all "Failed to
  load/authenticate boot image: Device Error" -> not image-specific; a step ABL runs before starting ANY image fails.
- Community (read 2026-11-08): github.com/Jem-isme/surface-duo1-brick-report - Duo 1 boot UFS unit is read-only until
  the Android kernel runs (matches our refused flashes); recovery works only while `fastboot boot` of TWRP works (then dd
  the OTA images + sideload, bootctl from Android); NO public Microsoft-signed EDL firehose; it WARNS that flashing
  back to the original locked state made a brick irreversible. XDA "Surface Duo 1 Super Partition Corruption Recovery
  (WOA #84)": "Failed to load image from partition: Load Error" = super LP metadata (ours says Device Error, super
  untouched). Microsoft Q&A: out-of-warranty repair via Microsoft support.
- STOPPED experimenting at 22:55 (no further boots/flashes: every remaining option either cannot work while ABL refuses
  all images or risks making it worse). Phone: fastboot, unlocked, current-slot a, battery 100 %.
- 2026-11-08 ~23:20, last low-risk tries (the tester's OK): `oem set-successful` -> "Cannot run Set-Successful in customer
  mode"; `flashing unlock_critical` (the tester ran it) -> "Device already : unlocked!" while device-info still says
  "Device critical unlocked: false"; flashing the identical vbmeta_a again -> "Error flashing partition : Device
  Error". Nothing in reach from fastboot changes the state. Left: full battery drain + cold boot; Microsoft repair.
- 2026-11-08 23:00-23:15, community fixes tried (all no change): WOA #91's `set_active other` + `fastboot boot twrp.img`
  (fresh cold boot, first command) -> "Failed to load/authenticate boot image: Device Error"; WOA #84 Fix 2's
  `fastboot erase misc` (OK) + set_active + `continue` (both slots) -> "Failed to load image from partition: Device
  Error"; **Microsoft's own 2023-08 boot.img** (extracted from the OTA payload with a stdlib extractor, sha256 = the
  payload's) RAM-booted on slot b, whose Microsoft vbmeta describes exactly that image -> same Device Error.
  => not image, vbmeta, slot or AVB-hash related: the bootloader fails EVERY boot at an image-independent step,
  persistent across cold boots, unreachable from fastboot in Customer mode. gus33000 (WOA #84): the 4th UFS LUN is
  read-only until the Linux kernel reinitialises it and slot state is recorded in TrustZone.
  Remaining: full battery drain + cold boot; WOA-Project (Telegram / gus33000) and XDA; Microsoft service if still
  offered for the Duo 1; a Microsoft-signed EDL loader (none public).

### Research after the brick (2026-11-08 ~23:30)
- Hands-off retry (nobody touching the phone): `set_active b` + `fastboot boot twrp.img` and the same on slot a ->
  both "Failed to load/authenticate boot image: Device Error". Earlier TWRP failures were not caused by handling.
- `getvar all`: partition sizes normal (boot 0x6000000, vbmeta 0x10000, dtbo 0x1000000 on both slots); slot a
  retry 7 / not successful, slot b retry 7 / successful / not unbootable. The duo-slot edit only set attribute bits.
- Closest public case: XDA GreemGreem, "Surface Duo 1 Super Partition Corruption Recovery" (thread 4786160): a 256 GB
  Duo where every `fastboot boot`/`continue` fails after a verification-on vbmeta was written over a rebuilt super.
  Their error is **Load Error**, ours **Device Error**. Same partition protection tiers (boot/vbmeta/dtbo refuse flash
  and erase), only `oem device-info` among OEM commands, EDL (05C6:9008) reachable but every public loader rejected
  (PK_HASH mismatch). Their phone is still unrecovered; the only route they see is a Microsoft-signed loader.
- Qualcomm's public ABL source (QcomModulePkg/Library/avb, e.g. the sdm710-lk mirror): a failed AVB verification,
  locked or not, returns EFI_LOAD_ERROR ("Load Error"); "Device Error" (EFI_DEVICE_ERROR) is not produced by the
  verification itself, so our failure (also on the very first locked attempt, which avbtool says verifies) sits in a
  later step that ABL runs for every image. Which one is not determinable from fastboot.
- Corrupting or clearing a slot to force the fallback (the tester's idea) cannot work here: fastboot cannot write or
  erase sde, verification-type failures do not use up retries, and the fallback target slot b fails the same way.
- Remaining: WOA-Project / XDA (draft for the tester's approval first), full battery drain + cold boot, Microsoft repair.
- More cases (2026-11-08 ~23:50), none recovered: Microsoft Q&A 2335881 (2022: Magisk written to the inactive slot after
  an OTA -> no Recovery, `fastboot boot` and `fastboot flash` both fail; only answer: Microsoft's EDL image is internal;
  "50+" duplicate threads); duo-de discussion 262 (the 256 GB Load Error Duo, no fix in the replies); duo-de #76 (an
  OTA problem, different). WOA RestoringBootloader.md: if a relock lands in the bootloader menu, "reunlock and undo
  the modifications" - for us that means rewriting boot_a/vbmeta_a, which needs a running OS. gus33000 in WOA #84:
  "There are other methods to fix this" (for the slot-state friction) - unpublished; ask him (draft first).
  Common thread of the unrecovered cases: a slot's boot-chain partitions changed by a running OS, then ABL refuses
  every image, also RAM boots, and fastboot cannot write the boot LUN.
- 2026-11-09 NEW SUSPECT (from qbootctl's source, installer.md "slot switching through qbootctl"): `duo-slot arm a`
  set only boot_a/boot_b's flags, leaving every other A/B pair (abl, xbl via the UFS boot LUN, tz, keymaster, vbmeta,
  dtbo, ...) active on slot b - a mixed slot state Android's boot control never writes, made with sgdisk (whole-GPT
  rewrite) on the boot unit. Untestable on our phone (it boots nothing). The installer now uses qbootctl's full switch.
- 2026-11-09 after a FULL battery drain (dead), 15-20 min wall charge, cold boot to Boot Select (battery 7 %):
  RAM boot of Microsoft's 2023-08 boot.img on slot b and on slot a -> "Failed to load/authenticate boot image: Device
  Error"; `continue` on b and on a -> "Failed to load image from partition: Device Error". Slot flags unchanged (a: not
  successful, b: successful, neither unbootable). The drain changed nothing. `erase metadata` / `erase userdata` were
  refused at 7 %: "Warning: battery's combined rsoc is very low" (retry above ~30 %; low odds - the 256 GB Duo's
  owner erased both without effect).
