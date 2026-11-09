# Driver 24 — Audio (speaker / headset / mic)

Status: ✅ **the speaker PLAYS under mainline Linux (2026-11-02, see [32](32-mainline-linux.md))** — hardware
and our device tree are proven. 🔧 Windows: **blocker identified (2026-11-01)** — the on-device speaker amp (WSA8815) is deliberately
disabled in gus's driver pack as a "workaround for WCD interrupt issue". Everything above the amp works.
Leading hypothesis: the codec's interrupt line is mis-declared in ACPI. Test firmware in progress.

## Hardware (from the iFixit teardown + Android device tree)
- **Codec:** Qualcomm **WCD9340** ("Tavil"), on Slimbus to the ADSP, SPI4 for WDSP control.
- **Speaker amp:** **one** Qualcomm **WSA8815** on the codec's SoundWire master, at
  `wsa881x@21170213`, prefix **SpkrLeft** (`qcom,wsa-max-devs = <1>`, `mono_speaker=left`).
- Board = **Surface Duo DV_A** (`/proc/device-tree/model`, `androidboot.dtbo_idx=31`).

## What already works on gus's FFU (Windows 24H2 26100.1742)
- Every audio PnP device is OK: Aqstic (QCOM051D), Audio DSP & Calibration Manager (SLM1 QCOM0524),
  Duo Aqstic Audio Device / codec (ADCM QCOM0525, `qcauddev8150.sys`), Audio Adapter / miniport
  (AUDD QCOM052C, `qcaudminiport8150.sys`), 2x Slimbus (QCOM0510), headset ctrl (QCOM0537),
  USB audio (QCOM0590), FSA4480 Type-C switch, Audio RPC daemon (QCOM058A).
- Endpoints: Speakers, Internal Handset Speaker, 3 mics. Default render = Speakers, 67%, not muted.
- **Test tone → silence.** gus's release notes since 2404 agree: "On device speakers are not
  functional"; headphones via the official USB-C dongle work since 2408 (same ADSP + codec).

## The blocker
`SurfaceDuo_auddevext.inf` (ext INF for ADCM\QCOM0525):
```
;Workaround for WCD interrupt issue for now
HKR, AUDD\CodecInitializationParameters, "SlaveInfo", %REG_DWORD%, 0x00000000
```
`SlaveInfo` = the codec's SoundWire slaves (the WSA amp). With 0 the stack comes up but the amp is
never enumerated → silent speaker. Reproduced: `SlaveInfo` 1 or 2 (± `IgnoreIrqs=1`) → codec init
fails, the miniport child (AUDD QCOM052C) never appears, 0 endpoints. Control (0) → 5 endpoints.

## Why: the codec interrupt pin (Android vs ACPI)
| Signal | Android DV_A device tree | gus ACPI `\_SB.ADSP.SLM1.ADCM.AUDD` | |
|---|---|---|---|
| Codec reset | TLMM 143 (`msm_cdc_pinctrl@143`) | `GpioIo` 0x8F | ✅ |
| WSA SD_N (amp enable) | TLMM 142 (`wsa_en_active`, output-high) | `GpioIo` 0x8E | ✅ |
| **WCD interrupt** | **TLMM 133** (`wcd9xxx-irq qcom,gpio-connect = <&tlmm 0x85>`) | `GpioInt` **0xC0** | ❓ |

0xC0 (192) is above the SM8150 TLMM range (0–174) — a Qualcomm-Windows "virtual/direct-connect"
number. Qualcomm's own `DSDT_8150_MTP.dsl` uses 0x100 for the same line. Whether 0xC0 really maps to
TLMM 133 is unverified → **test: firmware with AUDD GpioInt = 0x85 (plain TLMM 133) + `SlaveInfo`**.
(`0x85` also appears in `BTNS`, but that's on `\_SB.PM01` — the PMIC GPIO controller — no conflict.)

## Things ruled out (don't re-run)
- **Volume/mute/default endpoint** — fine.
- **TI-amp miniport** (`qcaudminiportti8150.sys`) — INF only selects it for builds < 19041.
- **Swapping ACDB calibration** — gus's ACDB is the generic **SC8180X.WP.1.0** (8cx Windows
  reference); Android's is **SDM855.LA.1.0** (Duo-tuned). The Windows ADCM rejects ANY Android file
  (full set, Codec+Speaker, Speaker-only, and LA→WP header-converted full set) → 0 endpoints. Reason:
  the Windows miniport requests ACDB device IDs (speaker **0x45 = 69**, plus 34/63/68/87/93…) that only
  exist in the WP set; Android uses 14/101. A real conversion would need device-ID + CDFT/CDOT remapping.
  ADCM reads its file list from `Enum\SLM1\…\Device Parameters\AudioCalibSettings\AcbdFilePath1..9`
  — repoint there, never edit the DriverStore.
- **`pnputil /restart-device` on the Qualcomm audio devices** → hangs → **BSOD 0x9F/4**. Reboot instead.

## Useful methods
- Tone in the signed-in session from SSH: `schtasks /create /tn ClaudeTone /tr "powershell -File
  C:\tone.ps1" /sc once /st 23:59 /it /ru <user>` then `schtasks /run /tn ClaudeTone`.
- Android vendor config from TWRP: parse the `super` LP metadata, `dd` `vendor_a` out by offset, read
  with `debugfs` in WSL (TWRP can't mount it). Key file: `etc/mixer_paths_tavil.xml` (speaker-mono:
  SLIM RX0 → CDC_IF RX0 → RX INT7 → COMP7 → SpkrLeft COMP/BOOST/VISENSE/SWR DAC_Port).
- Android device tree: boot image v2 `dtb` section (16 FDTs) + `dtbo` (36 overlays; DV_A = #31),
  decompiled with `dtc`.
- Mainline Linux has **no** Duo audio support (no sound nodes in `sm8150-microsoft-surface-duo.dts`).

## Interrupt-pin test (2026-11-01) — NOT the fix on its own
Built `_uefi_boot/uefi-AUDIO-gpio85.img`: upstream keys, DSDT binary-patched (exactly one
`GpioInt(GIO0, 0xC0)` descriptor → `0x85`, checksum fixed, verified by `iasl -d`). Ubuntu's iasl
(2020) can't recompile gus's DSDT.dsl (29 errors) — patch `builtin/DSDT.aml` instead (it is
byte-identical to the deployed table except the checksum byte). Build = stash only the Secure-Boot
files, copy patched aml, `PlatformBuild.py`, BootShim + mkbootimg, restore.
| Firmware | SlaveInfo | Endpoints |
|---|---|---|
| patched (0x85) | 0 | 5 (firmware harmless) |
| patched (0x85) | 2 | 0 |
| patched (0x85) | 1 | 0 |
| stock (0xC0) | 1 / 2 / 2+IgnoreIrqs=1 | 0 |
Codec device stays OK but creates **no children**; nothing in the event logs (silent in-driver fail).

Remaining codec switches with the amp enabled (`SlaveInfo=2`), all → **0 endpoints**:
| Firmware | SwrSleep | WcdInstanceOffset | Endpoints |
|---|---|---|---|
| stock (0xC0) | 1 | 0 | 0 |
| patched (0x85) | 1 | 0 | 0 |
| stock (0xC0) | 0 | 1 | 0 |
⇒ Every `CodecInitializationParameters` lever is exhausted (device restored to all-0 afterwards).
(Static analysis of the driver binary's trace setup was stopped by an automated check — not pursued.)

## Live Android reference (2026-11-01) — speaker WORKS in Android
Booted stock Android 12 (`2022.902.48`) by RAM-boot of `boot_a` on slot a. Getting there:
- `userdata` had to be **blanked** (not TWRP-formatted: TWRP's ext4 trips FBE `init_user0_failed`;
  TWRP also lacks `/dev/block/bootdevice` → `ln -s /dev/block/platform/soc/1d84000.ufshc
  /dev/block/bootdevice` before `twrp format data`). Blank = zero first 64 MB of `sda6` + all of `sda3`
  (metadata); Android then formats+encrypts on first boot.
- **Android boots only ONCE per blank** — any later boot hits `init_user0_failed` again (firmware runs
  from slot b, Android from slot a → FBE keys don't survive). Do a whole session per boot.
- Root: Magisk v31.1 patched `boot_a` → `(local file, not published)`, RAM-booted as
  the first boot; grant `com.android.shell` in Magisk's Superuser tab (v31.1 has no su prompt).
- Capture: `tools/audio/android-capture.sh` → `(local file, not published)`.

Findings (kernel `4.14.190-duo`):
- **SoundWire enumerates exactly one amp: `wsa881x.21170213`** on `swr0` (tavil SWR master).
  `ASoC: no sink widget found for SpkrRight IN` → Duo has only the left amp.
- WCD9340 = `chip id major 0x108, minor 0x1`, on Slimbus `171c0000.slim`, WDSP firmware `cpe_9340.*`.
- Speaker playback uses snd_device 2 (speaker) + 218 (vi-feedback) + path `spkr-vi-record`
  (WSA V/I sense → speaker protection).
- No debugfs/regmap on this production kernel; no tinymix binary.
**Hypothesis:** the 4 candidate WSA IDs are …0211/0212/0213/0214; laptops use 0211+0212, the Duo's amp is
**0213 (slot 3)**. If Windows `SlaveInfo` is a bitmask of expected slaves, the Duo value is **4** (bit 2),
not 1/2 (tested) → test `SlaveInfo=4` (then 8).

### SlaveInfo bitmask + interrupt-trigger tests (Windows, 2026-11-01)
| Firmware (AUDD GpioInt) | SlaveInfo | Endpoints |
|---|---|---|
| stock (Edge, 0xC0) | 4 | 0 |
| stock (Edge, 0xC0) | 8 | 0 |
| gpio85 (Edge, 0x85) | 4 | 0 |
| level-c0 (**Level**, 0xC0) | 4 | 0 |
| level-c0 (Level, 0xC0) | 2 | 0 |
| level-85 (Level, 0x85) | 2 | 0 |
| level-85 (Level, 0x85) | 4 | 0 |
⇒ **ACPI interrupt pin/trigger and every SlaveInfo value (1,2,4,8) are exhausted.** The failure is
inside gus's codec driver's SoundWire bring-up; no registry/ACPI lever reaches it. Device restored to
stock firmware + SlaveInfo=0.
Level-trigger rationale: Linux `wcd9xxx-irq` treats the WCD summary IRQ as level-high; gus's ACPI
declares it Edge — an edge never seen if the line is already high would hang SoundWire enumeration.
Images: `_uefi_boot/uefi-AUDIO-level-c0.img`, `uefi-AUDIO-level-85.img` (`amlpatch.py --level [--pin]`,
`build-audio.sh <aml> <out>`). All combos done (below).

## Linux route (2026-11-01)
1. **Android's kernel = a working Linux driver for this exact hardware.** Boot Android (needs the
   post-FFU Factory data reset), then read dmesg (wcd934x / swr / wsa881x), `tinymix` controls and
   regmap debugfs while the speaker plays → the exact amp bring-up and SoundWire device ID.
2. **Mainline Linux:** upstream `wcd934x` + `wsa881x` drivers; closest board = **SDM845 db845c**
   (same WCD9340 + WSA881x). Port its sound nodes into `sm8150-microsoft-surface-duo.dts` (which has
   none), boot with `fastboot boot`.

## Inside qcauddev8150.sys (capstone, ARM64)
- Registry reader @0x38680…: `CodecInitializationParameters` → global cfg block (ptr @RVA 0x27ea8):
  +0x24 IgnoreIrqs, **+0x28 SlaveInfo**, +0x2c SwrSleep, +0x30 WcdInstanceOffset.
- @0x3eb38 copied into a stack struct (SlaveInfo → struct+0x38) → `bl 0x58d18` (codec init) →
  second struct (SlaveInfo → +0x28) → `bl 0x6e6a0` (next stage; arg saved to [sp+0x40]).
  Static taint past this point is unreliable — next step is dynamic (WPP trace).
- Driver logs via WPP (`EtwRegister` + WppRecorder IFR; log calls `bl 0x4118`/`0x2980` with message #
  in w3, status in w5). Control GUID not yet extracted (registered via table, not a direct call).

## Next
1. Get the WPP control GUID (WppInitKm control block / `WPP_CONTROL_GUIDS`), set an ETW AutoLogger
   for it, boot with `SlaveInfo=2`, read raw message numbers + NTSTATUS → which init step fails.
2. Check SoundWire slave address: Android amp = `0x21170213` (of candidates 0211/0212/0213/0214);
   look for those constants in qcauddev8150.sys.
3. Share findings with WOA-Project (they know the "WCD interrupt issue") — pin map + SlaveInfo trace.

### Correction (2026-11-03): no Windows codec driver exists
gus (WOA-Project/SurfaceDuo-Guides#70, Sep 2024): *"no code was made to support the WCD chip. So it is not working
because no code exists to make it work."* The Qualcomm audio stack in the pack exposes codec init parameters
(`SlaveInfo` etc.), but nothing drives the WCD9340/WSA8815 the way Android and our mainline Linux do — so no registry
value can fix it (consistent with every `SlaveInfo` test failing). The Windows route is a new driver (KMDF) that
brings up the codec on Slimbus and the amp on the codec's SoundWire, following the captured Linux recipe
(`(local file, not published)`, stream start/stop trace). Open question: whether such a
driver can coexist with qcauddev (which owns the Slimbus controller) or must replace that part of the stack.

### Windows design options (2026-11-03, offline)
Key fact from the Linux capture: **every WSA8815 register access goes through WCD9340 registers** (the codec's
SoundWire master is driven via `WCD934X_SWR_AHB_BRIDGE_WR_ADDR/WR_DATA/RD_ADDR/RD_DATA/ACCESS_CFG/STATUS`). So the
amp needs no new bus driver — only a way to write codec registers in order, after qcauddev has brought the codec up
with `SlaveInfo=0`, plus the RX7/COMP7/SPKR_DRV_EN path toggles at stream start. Routes, cheapest first:
1. **Calibration/RTAC path:** the "Audio DSP & Calibration Manager" (SLM1, QCOM0524) exists so Qualcomm's QACT tool
   can peek/poke codec registers at run time. If its IOCTL interface is reachable from a user-mode helper, the
   whole amp bring-up could be a small user-mode service. Next: enumerate the device interfaces/IOCTLs the
   qcauddev/SLM1 drivers expose (Windows side, needs the phone in Windows).
2. **Upper/lower filter on ADCM (QCOM0525):** a KMDF filter that issues the codec-register writes through the
   same stack qcauddev uses, once at D0 and at stream start.
3. **Replace the codec part:** our own WCD934x driver on Slimbus — largest; conflicts with qcauddev, which owns
   the Slimbus controller.
Prerequisite for all: Windows still boots after the partition shrink (✅ 2026-11-03, ESP swap + stock UEFI).

### Route 1, step 1: the stack's private interfaces (Windows session 2026-11-04)
`tools/windows/duo-audio-interfaces.ps1` (devices, services, driver images, every device interface) +
`tools/audio/drvscan.py` (which driver binary holds each interface GUID; keyword strings). Outputs in
`(local file, not published)` (drivers themselves: `(local file, not published)`, never committed).
- **Codec = ADCM\QCOM0525 "Surface Duo Aqstic Audio Device", service qcauddev (qcauddev8150.sys)**; it registers 4
  private interfaces: `0918e253…` and `5813898d…` (also referenced by the miniport = its client link),
  **`7addc8e2-cc7c-4d07-8f04-105423b8b094` and `a5241c7a-c5f1-4533-8c74-32d057249aeb` (referenced by NO other
  driver of the stack → a user-mode / tool-facing door: first candidates for a codec-register IOCTL)**.
- **Calibration manager = SLM1\QCOM0524, service qcadcm (qcadcm8150.sys)**: `4ee2204f…`, `5f51a34c…` (both opened by
  qcauddev), `8feaf834…` (in no binary: class GUID from the INF). Strings: acdbdelta, avs_config.acdb, AUDIOCALIBSETTINGS.
- qcauddev opens the Slimbus bus interface `fadbf586…` (qcslimbus8150) — it is the Slimbus client for the WCD.
- qcauddev DOES contain WCD code (paged sections `PAGEwcda`/`PAGEwcdd`) and reads `Device Parameters\AUDD\…`:
  BUSINFO {Bustype=0, **AHBIRegistryAddr=0**}, CodecInitializationParameters {Major 3 Minor 2, mclk 2/0x10001, vdd_buck 1,
  IsWDSPAvailable 1, IsMBHCReq 1, SlaveInfo 0, …}, SpeakerProtectionParameters (SpkrProt*), HW_PLATFORM 1, GPIO/power
  packages (full dump: `windows-codec-device-parameters.txt`).
- **Step 2 (offline, `tools/audio/ioctls.py`, outputs `qcauddev8150-ioctls.txt` / `qcadcm8150-ioctls.txt`):** the stack
  uses a private IOCTL device type **0xADAE**, all METHOD_IN_DIRECT. **qcauddev8150 HANDLES none of them** — its 15
  codes (fn 0x80f–0x848) are all built right before a call = requests it SENDS. **qcadcm8150 (calibration manager) is
  the hub: 78 distinct codes compared in its dispatchers** — fn 0x800–0x84b (the qcauddev client range) and
  fn 0x92c–0x986 (a second range: likely the user-mode/QACT side). fn 0x810/0x811/0x812 are sent by qcauddev but not
  handled in qcadcm (another target: miniport/ADSP). No public headers for any of this (web search 2026-11-04).
  So route 1 = qcadcm's interfaces (4ee2204f, 5f51a34c, 8feaf834), not qcauddev's. Next: decode qcadcm's dispatcher
  0xb958 (fn 0x92c–0x93d) + 0x89c70 handlers — find one that forwards a codec register address/value down to
  qcauddev (which owns the Slimbus link to the WCD), then test it from user mode on a harmless read (WCD chip ID).
- **Step 3: qcauddev DOES contain the WSA path code (2026-11-04, `tools/audio/immscan.py` + `callers.py`).** The
  codec's SoundWire AHB-bridge registers (downstream layout: WR_DATA 0xc85.., WR_ADDR 0xc89.., RD_ADDR 0xc8d..,
  RD_DATA 0xc91.., ACCESS_CFG 0xc95, ACCESS_STATUS 0xc96) are used in exactly two functions: **0x9a0a8 = bridge WRITE**
  (0xc85 + 0xc95 + 0xc96) and **0x9a488 = bridge READ** (0xc8d + 0xc95 + 0xc96). Callers: a cluster 0x979b0–0x9d690
  (several entry points are callbacks, e.g. 0x7e270, 0x9c2a0) = **a SoundWire master driver over the WCD's bridge**,
  and the generic codec-register write 0x712b8 (34 callers, incl. the SlaveInfo consumer 0x58d18). So gus's "no code
  exists" is not literally true: the SWR master + bridge accessors are compiled in; what fails with SlaveInfo≠0 is
  somewhere in that path (enumeration/IRQ), not missing code.
  Ruled out this round: (a) amp held in shutdown — the codec's 2 GPIOs (Device Parameters AUDD\GPIO: index 0 init 0,
  index 1 init 1) are the DSDT's GpioIo 0x8F (reset 143) + 0x8E (WSA SD_N 142) already in the table above;
  (b) WSA SoundWire IDs as constants — 0x2110 / 0x21170213 absent; the 0x211–0x215 hits are WPP log message numbers
  (`mov w3,#..; bl 0x2a68` = trace call).
  Next: dynamic — read the WPP/IFR log for the SlaveInfo≠0 failure (EnableIFRLogging=1 is set; find the WPP control
  GUID passed with the 0x2a68 trace calls) to see which SWR step fails; or statically walk 0x9ae90 (8 callers: SWR
  register access) → enumeration loop.
- (superseded) find qcauddev's IRP_MJ_DEVICE_CONTROL / WDF queue for the 7addc8e2 and
  a5241c7a interfaces and decode the IOCTL codes + buffer layouts (look for codec read/write handlers next to the
  `PAGEwcd*` code); then a user-mode test reading one known WCD934x register (chip ID) through it.
