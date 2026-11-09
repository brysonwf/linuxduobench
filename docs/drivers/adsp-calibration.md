# Workbench — ADSP audio calibration (Android parity item 4, 2026-11-07)

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)).
the tester 2026-11-07: "Build a driver for the audio processor". Goal: the speaker gets Microsoft's own tuning (EQ,
multiband compressor, limiter, speaker protection) on the ADSP, like Android - not our PipeWire stand-in EQ.

## Where Android's tuning lives
The phone's vendor_a `/etc/acdbdata/Surface/region/{a,b}/SURFACE_*_cal.acdb` (Speaker 570 KB, Handset 790 KB, ...;
also generic QRD/CDP sets). Platform "SDM855.LA.1.0", ADSP "ADSP.BF.2.4", codec "WCD9340.1.0", OEM info "1.0 FLUID",
modified 2021-6-23. Local copy (never committed): (local file, not published) Android's loader (libacdbloader)
is closed; no open parser for this "QCMSNDDB" format exists (searched 2026-11-07).

## File format (tools/linux/audio-tuning/acdb-dump.py)
`QCMSNDDB` + 8 bytes, then `AVDB` + three u32 (0, size, size), then chunks: 8-byte ASCII tag, u32 length, payload.
Header chunks: MODIFIED, SWPNAME, SWPVERS, OEMINFO, DEVCATIN ("Speaker"), DPROPLUT (device properties), ADSPVERS,
CDCNAME, ACSWVER2. Calibration comes in triples per category:
- `xxxxLUT0` lookup table (rows of u32: device / app / sample-rate keys ...),
- `xxxxCDFT` calibration data FORMAT table (count + module/param ID pairs),
- `xxxxCDOT` calibration data OFFSET table (count + offsets into DATAPOOL),
- `DATAPOOL` (88 KB) = the parameter blocks.
Categories seen: AVOL (audio volume), AFE (port), VDPI, ADST, VVP3/AVP3/EVP3 (topologies?), AGII, AVII, AFEI, ASTI,
LSMI, VSTI/VDYI (voice stream / dynamic, with VSTIOFST/VDYIOFST + CVD), VS2I, ANC, CDFS.
IDs recognised so far (Qualcomm ADSP module IDs, from memory - to be confirmed against the DSP's replies):
0x00010BFE volume control; 0x0001026A + 0x0001026C/D/F in the AFE tables = speaker protection (feedback) module +
params. Other IDs (0x000113B8, 0x00010EF6, 0x00010C29, ...) still to identify.

## The speaker's keys (2026-11-07)
- vendor `audio_platform_info.xml`: SND_DEVICE_OUT_SPEAKER = ACDB id **14** (0x0e), SND_DEVICE_OUT_SPEAKER_PROTECTED =
  **101** (0x65); speaker bit width 24. The LUT rows carry exactly these ids (e.g. AFE LUT0 rows `0b 0e 00 ..`,
  `.. 65 ..`) -> rows are keyed by ACDB device id; decode the row layout per table next.
- DATAPOOL holds length-prefixed UTF-16 labels for the use-case/topology names; the playback ones:
  SPKR_PHONE_SPKR_MONO_RX_FB_PROT (speaker with feedback protection = the Duo's one WSA8815),
  SPKR_PHONE_SPKR_MONO_TX_FB_PROT (its VI feedback path), plus STEREO/MONO_2 variants and RAS test entries.
- Local copies (never committed): (local file, not published) audio_platform_info.xml, audio_effects.*}.

## Table layout DECODED + the speaker's calibration (2026-11-07 late; tools/linux/audio-tuning/acdb-cal.py)
- AGIILUT0 (device/COPP) rows [dev, rate, app_type, cdft_off, cdot_off]; AFEILUT0 (AFE port) [dev, rate, cdft, cdot];
  AVIILUT0 (per volume step) [dev, app_type, vol_index 0..15, cdft, cdot]; AVOLLUT0 [dev, app, step, ?, ?];
  VSTI/VDYI = voice. CDFT at an offset = count + [module, instance, param] triples; CDOT = count + DATAPOOL offsets;
  DATAPOOL entry = u32 size + payload. App type 0x11130 = 69936 (default playback, from memory).
- Speaker (14) / protected speaker (101), 48 kHz, app 0x11130 - IDs identified from memory of Qualcomm's
  apr_audio-v2.h, to be confirmed by the DSP's replies:
  * per VOLUME STEP (stream/POPP level): module 0x0001031F = multi-channel IIR tuning filter (EQ): params 0x1031C
    enable, 0x1031D pre-gain, 0x1031E filter config (14: 32 B, 101: 140 B = more stages); module 0x00010943 (params
    0x10944/45/46: enable + config 132 B / 692 B) = probably a multiband compressor (MBDRC). Tuning changes with the
    volume step.
  * device (COPP): 0x00010719/0x10911 (resampler?), 0x00010C2A = bass enhancement (enable 0 = off, config 176 B),
    0x00010C35/0x10C36 (gain-like: 0x11fe for 14, 0x2000 for 101).
  * AFE port: module 0x0001025F with params 0x10203 enable + 0x10261..0x10266 + 0x102B0/B1/B5/B6 (124-940 B blocks)
    = a big speaker-port stage (speaker protection / feedback config? to identify).
- Next: identify each module against the DSP (ASM/ADM GET_PP_PARAMS replies or "module not found" errors), find the
  POPP/COPP topology ids that contain these modules (AVP3/VVP3 tables), then the kernel path that opens with that
  topology and sends the blocks.

## Topologies: the complete map (2026-11-07 night)
Global file (SURFACE_A_Global_cal.acdb, container "GCDB"): GPROPLUT = global property id -> DATAPOOL offset; the
DATAPOOL names every custom topology with its id and module list, e.g.
AUDIO_RX_MONO_COPP_MBDRC_V4 0x10010313, AUDIO_RX_STEREO_COPP_MBDRC_V4 0x10010314, AUDIO_RX_MONO_COPP_MBDRC_V3
0x11000001, AFE_RX_FF_SPKR_PROT_TOPOLOGY_V3 0x1001025E, AFE_RX_FB_SPKR_PROT_TOPOLOGY_V3 0x1001025D,
AFE_TX_FB_SPKR_PROT_TOPOLOGY_V3 0x1001025C, AUDPROC_OFFLOAD_EFFECTS 0x1000FFFF, VOICE_RX_Default_V3_MBDRC_V4 0x10010F8B.
Topology definition blobs (to register with the DSP): global props 0x12E47 (16860 B, COPP/POPP topologies) and
0x12E48 (2356 B, AFE topologies?) - to confirm.
Speaker device file DPROPLUT (rows [dev, prop, DATAPOOL offset]) for dev 14 / 101:
- 0x113AD: app type -> COPP topology: 0x11130..0x11133 -> **0x10010313** (mono, MBDRC v4) for both.
- 0x13150: AFE topology: dev 14 -> **0x1001025E** (FEED-FORWARD speaker protection - no VI feedback needed),
  dev 101 -> 0x1001025D (feedback protection).
- 0x113AF: voice topology 0x10010F8B; 0x113B8: the device's name; 0x12EED: supported sample rates (48000, ...).
=> Android's speaker = device 14 or 101; the plain speaker (14) needs no feedback path.

## Topology definitions decoded (acdb-topologies.py)
Global props 0x12E47 (43 ADM/ASM topologies), 0x130DE (3 AFE topologies), 0x12E48 (6 more): fixed 392-byte slots =
[topology id, module count, 16 x 24-byte module records (module id, 0x10000 = instance/flags, 16 bytes init params)]
= the ADD_TOPOLOGIES payload format. The speaker's:
- COPP **0x10010313** = 0x10719 (resampler), 0x10C2A (bass enhancement), 0x1031F (multi-channel IIR), 0x10943
  (MBDRC), 0x10C35 (gain) - exactly the modules the AGII (device) + AVII (volume step) blocks target.
- AFE **0x1001025E** = 0x1025F - exactly the module of the AFEI blocks (10 params). 0x1001025D (feedback protection,
  dev 101) = 0x1025F too; 0x1001025C (the VI feedback TX path) = 0x1026A + 0x1026F.

## The driver, concretely
1. At start-up register the custom topologies with the DSP (ADM/AFE ADD_TOPOLOGIES with the 0x12E47/0x12E48 blobs) -
   what the closed acdb loader does on Android.
2. Open the speaker path with them: COPP 0x10010313 (q6adm), AFE port topology 0x1001025E (q6afe) instead of the
   null topologies mainline uses.
3. Send the parameter blocks: AGII (device) + AVII (volume step) to the COPP (ADM SET_PP_PARAMS), AFEI to the AFE
   port (module 0x1025F). Source = a calibration file generated on the phone from its own acdb (acdb-cal.py --dump).

## DSP test: which COPP topologies open (2026-11-07 night, test knob q6routing.copp_topology)
Speaker path set like audiotest.sh but RX7 Digital Volume 0 + PA Volume 0, all-zero samples (silent):
- 0x00010312 (NULL, mainline default): opens. - 0x00010BE3 (Qualcomm's built-in default audio COPP): opens.
- **0x10010313 (Microsoft's speaker topology): ADM DEVICE_OPEN error 3** = not known to the DSP until registered.
(Without the codec routing even the NULL open failed: AFE port 0x4000 start error 9 - the SLIMbus path must be set.)
Next, two routes:
A. Faithful: register the custom topologies (ADM_CMD_ADD_TOPOLOGIES 0x00010335 / AFE add-topologies) with the
   0x12E47 / 0x130DE blobs. Out-of-band only: buffer in the ADSP's IOMMU space -> ADM_CMD_SHARED_MEM_MAP_REGIONS
   (0x00010322, rsp 0x00010323 = handle) -> ADD_TOPOLOGIES {addr lsw/msw, handle, size}. q6adm has no IOMMU-mapped
   device; mainline maps buffers only for q6asm (the q6asm-dais node's iommus) - allocate from that context or give
   q6adm one in the DT.
B. Shortcut to evaluate first: 0x00010BE3 opens - if it contains the IIR (0x1031F) + MBDRC (0x10943) modules, send
   Microsoft's blocks to it with ADM_CMD_SET_PP_PARAMS_V5 (in-band payload possible) - needs a GET_PP_PARAMS probe.

## Route B probe + registration test (2026-11-07 night, test branch only)
- q6adm GET_PP_PARAMS_V5 probe (in-band reply): NULL topology 0x10312 has resampler 0x10719 + volume 0x10BFE; the
  built-in default 0x10BE3 adds bass enhancement 0x10C2A; NEITHER has the IIR 0x1031F, MBDRC 0x10943 or gain 0x10C35
  (status 3) -> route B is out, the custom topologies must be registered.
- Registration WORKS at the command level: routing node given the msm-audio-ion SID (iommus 0x1b21, same iommu group
  as q6asm-dais), dma_alloc_coherent on it, ADM_CMD_SHARED_MEM_MAP_REGIONS (pool 3) -> handle 0xb08f4d68, then
  ADM_CMD_ADD_TOPOLOGIES with the 0x12E47 blob (16860 B) -> status 0 (same packet as downstream
  send_adm_custom_topology: addr lsw/msw + handle + size, data passed raw).
- BUT opening a COPP with ANY registered custom topology - the speaker's 0x10010313 and even the 1-module
  0x10000015 (resampler only) - FROZE THE SoC (no log; the watchdog reset it, the PC watcher re-booted the default).
  => the DSP accepted the blob's size but parses the definitions differently. Suspects: this ADSP's ADM API version
  wants the v2 layout (newer downstream q6adm picks the format by q6core's avcs api version for the ADM service), or
  the right blob is another global property. Do NOT retry blindly (each try = a freeze).
- Next: learn the exact format from a working system - Droidian (downstream 4.14, audio works) on sda9: dynamic debug
  for q6adm's custom-topology send (size, version path), q6core's avcs version for ADM, and if possible the cal buffer
  itself; plus the downstream SM8150 headers (apr_audio-v2.h: audproc_topology_definition / module info, v1 vs v2).

## Why the freeze: this ADSP registers topologies through AVCS (2026-11-07 late)
- Downstream SM8150 kernel (LineageOS android_kernel_xiaomi_sm8150, techpack/audio): send_adm_custom_topology sends
  exactly our packet (legacy path), but the vendor acdb loader (libacdbloader.so on vendor_a, strings) also has
  acdb_loader_send_common_custom_topology / ACDB_CMD_GET_AVCS_CUSTOM_TOPO_INFO_SIZE_V3 / "CORE_CUSTOM_TOPOLOGIES":
  newer ADSPs register custom topologies ONCE through the core service (q6core):
  q6core_send_custom_topologies = AVCS_CMD_DEREGISTER_TOPOLOGIES 0x1292A (mode 2 = all custom) ->
  AVCS_CMD_SHARED_MEM_MAP_REGIONS 0x12924 (pool 3; rsp 0x12925 = handle) -> AVCS_CMD_REGISTER_TOPOLOGIES 0x12923
  {addr lsw/msw, handle, size} -> AVCS_CMD_SHARED_MEM_UNMAP_REGIONS 0x12926 (the DSP copies the definitions).
- The payload is NOT a raw acdb property: global prop 0x1308F (9114 B) holds the topology list in the ACDB's annotated
  format (version 1, count 0x41, 160-byte records [id, name length, UTF-16 name, ...]); the closed libaudcal.so /
  libacdbloader.so convert it to the AVCS "V3" layout. Local copies (never committed):
  (local file, not published)
  avcs-topologies.bin (= prop 0x1308F), adm-topologies.bin (= prop 0x12E47).
- Next (offline, no risk to the phone): reverse the conversion in libaudcal.so (AcdbCmdGetAvcsCustomTopoInfo* /
  the V3 builder) with objdump/Ghidra -> write our own converter (acdb-cal.py) -> register through a q6core
  AVCS path (mainline q6core.c has the APR device; add the 4 commands) -> COPP 0x10010313 -> SET_PP_PARAMS.
  The ADM ADD_TOPOLOGIES test stays on the side branch only (it freezes the SoC on open).

## The AVCS V3 payload = global prop 0x132AC, raw (2026-11-07, libaudcal.so disassembly)
`AcdbCmdGetAVCSCustomTopoInfo_V3` (libaudcal.so 0x200f4) loads property 0x000132AC via acdbdata_ioctl and memcpy's it
out unchanged (no conversion); the non-V3 `AcdbCmdGetAVCSCustomTopoInfo` uses 0x000131A7. 0x132AC (3436 B) =
u32 count (58), then records [u32 2, topology id, u32 type (1 = COPP, 2 = AFE, 4 = LSM, from the examples),
u32 n_modules, n x (module id, 0x10000)] - e.g. `2 10010313 1 5 | 10719 10C2A 1031F 10943 10C35` (the speaker) and
`2 1001025C 2 2 | 1026A 1026F`. Local copy: (local file, not published)
Test kernel `-avcs` (side branch): q6core gains the 4 AVCS commands + q6core_register_custom_topologies(); q6adm
registers the V3 blob on the first COPP open.

## DEFAULT since 2026-11-07 ~22:45: the speaker plays with Microsoft's own DSP tuning
Kernel 6.19.0-00072-g1fa0d128a396 (series 68-72, no knobs, all from the DT):
- q6core `q6core_register_custom_topologies()` / `_ready()`; q6routing registers on the speaker backend's first
  playback hw_params (files appear at run time; again after a DSP restart) and opens COPP 0x10010313 + sends
  speaker-copp.bin (`q6adm_set_pp_params()`); q6afe-dai starts SLIMBUS_0_RX with AFE topology 0x1001025e +
  speaker-afe.bin; sdm845 reads `qcom,channels = <1>` / `qcom,bits-per-sample = <24>` from the speaker dai-link.
  Missing files or a refusing DSP = untuned as before.
- Files: `duo-speaker-tuning` (tools/linux/audio-tuning) from the phone's ACDB on vendor_a, 0.5 s, region from
  `androidboot.hardware.audio.tuning` (this phone: **b**, read on an ABL boot; for the speaker a and b are
  byte-identical), makeup gain +8 dB in the tuning's own gain stage (0x10C36 0x11fe -> 0x2d32, after the MBDRC, so the
  untuned fallback keeps today's level). Run by /init (workbench, tools in /lib/duo/audio-tuning) and pmOS duo-hw,
  which also renames PipeWire's stand-in EQ to .off while the DSP tuning exists (duo-health checks "never both").
- Checks: pmOS 33 PASS / 0 FAIL, `custom topologies (3436 bytes): 0` on the first sound, pw-play rc 0; workbench
  plain reboot generates it at 4.8 s. the tester's listening check of the clean default: "sounds good to me".
- Open: 24-bit is set but the FE still opens 16-bit (q6asm upsamples: fine); the AFE FEEDBACK protection (device 101,
  VI sense + persist calibration) is the next step up; headset/BT paths untouched.

## AVCS registration WORKS - the freeze was a missing SMMU stream ID (2026-11-07 late night)
First try: deregister 0, map 0, then REGISTER timed out (-110) and the SoC froze. Cause: the DSP chooses the SMMU
context from the stream ID carried in the upper 32 bits of every buffer address (downstream
`msm_audio_populate_upper_32_bits()`; mainline q6asm-dai does `addr | (sid << 32)`, sid = `iommus` arg & 0xF). Our
map/register sent the bare IOVA `0x00000000ffdff000`, so the DSP read through the wrong context -> fault -> stall.
Fix: OR sid (0x1b21 & 0xF = 1) into `msw`. Result on test kernel 6.19.0-avcs-00068-g8ff2957a97a2:
`avcs map 4096 bytes at 0x00000000ffdff000 sid 1: 0`, `avcs register 3436 bytes: 0 status 0x0`; the vendor speaker
COPP topology **0x10010313 opens** and plays (unregistered it fails cleanly with DSP error 3, no freeze); GET_PP_PARAMS
answers for modules 0x10943, 0x10C2A, 0x10C35, 0x10719 (0x1031F rejects our guessed param 0x1031C: wrong param id).
The earlier ADM_CMD_ADD_TOPOLOGIES freezes most likely had the same missing-SID cause. Next: SET_PP_PARAMS with the
speaker's AGII/AVII blocks (out-of-band buffer, same SID rule), then the AFE blocks, then a listening test.

## The speaker's COPP calibration LOADS (2026-11-07 ~22:15, test kernel 6.19.0-avcs-00068-g855881aae59a)
`acdb-cal.py <Speaker_cal.acdb> 14 --copp-blob speaker-copp.bin` (new options --step / --copp-blob / --afe-blob) packs
the AGII device blocks + AVII volume-step blocks as one set-param payload [module, param, u16 size, u16 0, data]
(668 B, 10 blocks; the AVII blocks are the same for every volume step on device 14). Test q6adm sends it in-band with
ADM_CMD_SET_PP_PARAMS_V5 (0x10328) right after opening COPP 0x10010313 (knob q6adm.copp_cal), firmware path
qcom/sm8150/microsoft/speaker-copp.bin (made on the phone/PC from the phone's own ACDB, never in the repo).
Result: `copp cal 668 bytes to topology 0x10010313: status 0x0`; GET_PP_PARAMS read-back before -> after:
gain 0x10C35/0x10C36 `0x2000` -> **`0x11fe`** (= the ACDB value), IIR 0x1031F/0x1031C DSP default -> the ACDB's
`01 00 00 00 03 00 00 00 ...` (enabled, 3 bands); MBDRC enable already matched; aplay rc 0, no freeze.
GET_PP error 0x12 earlier = ADSP_ENEEDMORE (our 24-byte read was smaller than the 68-byte param), not a missing module.
**Measured A/B (phone's own mics, Farina sweep, 0/1/0/1 runs, RX7 80 + PA 12; test kernel g1d8ae649f2ed, whose
copp_topology knob touches playback COPPs only - the first A/B also put the MIC path through the speaker tuning and
was invalid):**

| sweep input | level, cal vs none | 100-126 Hz (rel. to 1-4 kHz) | top octaves |
|---|---|---|---|
| -12 dBFS | -12.4 dB | +5..6 dB | -13 dB |
| -30 dBFS | -3.6 dB | +9..12.5 dB | -5.3 dB |

=> Microsoft's speaker tuning is a level-dependent multiband DRC (MBDRC 0x10943, one config for every volume step;
its 0x10945 block has a field 0x0b44 = 0.088 Q15 = -21 dBFS, likely the limiter threshold): strong bass lift at low
levels, loud passages held down (speaker protection). The IIR (0x1031F) is disabled in the tuning; bass enhancement
(0x10C2A) off; gain 0x10C35 = -0.14 dB. Android's mixer path adds no makeup gain (speaker-mono: RX7 Digital Volume 84,
WSA PA Gain 0 dB). Side notes: without the tuning the sweep's top end clips the phone's mic; a ~0 dBFS click appears
in the first 10 ms of some recordings (capture start transient, both modes - not the speaker).
**AFE stage loads too (test kernel g2d2a857c0505, knobs q6afe.afe_topology / afe_cal, SLIMBUS_0_RX 0x4000 only):**
before port start, AFE_PARAM_ID_SET_TOPOLOGY (0x1025A in AFE_MODULE_AUDIO_DEV_INTERFACE 0x1020C, {1, 0x1001025E})
then `acdb-cal.py ... --afe-blob speaker-afe.bin` (11 blocks of AFE_MODULE_FB_SPKR_PROT_V2_RX 0x1025F, 1504 B) in-band
with AFE_PORT_CMD_SET_PARAM_V2 -> `topology 0x1001025e: 0 status 0x0`, `cal 1504 bytes: 0 status 0x0`, port runs.
The AVCS blob holds both AFE protection topologies (0x1001025E FF, 0x1001025D FB = module 0x1025F) and the VI TX one
(0x1001025C = 0x1026A + 0x1026F). Measured with the COPP tuning on in both runs: level within 1 dB at -12 and -30
dBFS input; about +3 dB at 1.3-1.6 kHz, -2 dB at 3-8 kHz (protection mostly acts at high excursion/temperature).
Order note: the first SET_TOPOLOGY went out before the AVCS registration (it runs on the first COPP open) and was
still accepted; the real driver registers at ADSP start-up.
**Listening verdict (the tester, 2026-11-07 22:05; RX7 80 + PA 12, A = stock null topologies, B = COPP 0x10010313 +
tuning + AFE 0x1001025E + protection):** pink noise "A was clear fuzz"; ringtone + voice: **"A sounded louder and
better"**. => The DEFAULT STAYS UNTUNED. Microsoft's tuning is a protective/loudness DRC that costs ~12 dB on loud
material, which Android likely made up elsewhere (stream volume curve) or simply accepted. The whole chain works and
stays an opt-in capability (test branch -avcs, knobs q6routing.copp_topology / q6adm.copp_cal / q6afe.afe_topology +
afe_cal); not folded into the series. Possible later: the AFE protection alone at higher levels (it barely changes
the sound and protects the speaker), or the tuning with makeup gain - only if a listening test prefers it.
Driver shape if it is ever upstreamed (below).

**CORRECTION, same evening - the tuning WINS once played in Android's format (the tester: "it sounded grainy and unclear
and low like it was not a perfect implementation" -> "b sounded good just not as loud" -> "B is def better"):**
- Grain = our stereo backend. Topology 0x10010313 is Microsoft's MONO chain and its blocks address one channel
  (type 3 = FC); Android's speaker-mono path sets `SLIM_0_RX Channels = One`; mainline's sdm845 BE fixup forces 2 ch.
  Test knob `snd_soc_sdm845.spk_channels=1` (SLIMBUS_0_RX only; q6dsp maps 1 ch to FC) -> clean.
- Low = the MBDRC limiter (~-21 dBFS) leaves ~12 dB of headroom -> makeup gain on the codec: RX7 Digital Volume
  92 (= +8 dB vs 84; the A/B ran A at 80, so +12 dB relative). Peaks stay below the untuned default's full scale.
- 24-bit (Android's speaker bit width): WORKS when it is the FIRST width used on SLIMBUS_0_RX in a boot (fresh boot:
  24-bit mono untuned and fully tuned both start, rc 0). Switching width within a boot breaks: 16 -> 24 gives AFE
  DEVICE_START (0x100e5) error 1; 24 -> 16 with the AFE protection topology set CRASHED the ADSP ("audio_process"
  exception, remoteproc recovered it). => the speaker backend's format must be FIXED (mono, S24_LE) for the whole
  boot - set it in the DT, never switch. S32_LE: the codec refuses it ("Invalid format 0x20").
- WINNING CHAIN (round 4): AVCS registration + COPP 0x10010313 + speaker-copp.bin + AFE 0x1001025E +
  speaker-afe.bin + mono SLIMBUS_0_RX + RX7 92 (PA 12). Next: make it the default, in clean form (driver shape below):
  topology registration at ADSP start-up, DT-named topology/cal files, a mono speaker backend, UCM gain, and the
  calibration files generated on the phone from its own ACDB at first boot.

## Driver shape for the series (draft)
- q6core: `q6core_register_custom_topologies()` (AVCS deregister/map/register/unmap, SMMU SID in the address) - generic.
- Who calls it, and with what: a DT property on the q6core/apm node naming a firmware file (e.g.
  `qcom,custom-topologies = "qcom/sm8150/microsoft/avcs-topologies.bin"`), registered once after the ADSP is up.
- q6adm / q6afe: per-backend topology + calibration file named in the sound card DT (dai-link or backend properties:
  COPP topology 0x10010313 + speaker-copp.bin, AFE topology 0x1001025E + speaker-afe.bin), sent at COPP open / port
  start - not a module knob.
- The files: made on the phone from its own ACDB (vendor_a) by acdb-cal.py at first boot (pmOS service), stored in
  the root's /lib/firmware; nothing vendor-derived in the repo or the images. (module 0x1025F, AFE topology 0x1001025E via
AFE_PORT_CMD_SET_PARAM_V2), then fold it into the series in upstream shape.

## What mainline can do today (6.19)
q6asm opens every stream with ASM_NULL_POPP_TOPOLOGY (no post-processing modules); q6adm opens COPPs with a topology
id but has no SET_PP_PARAMS; q6afe has AFE set-param internally only. So nothing can load calibration.

## Plan
1. Decode the tables: for the speaker device, which topology (ASM POPP / ADM COPP) and which module/param blocks
   (LUT row -> CDFT entry -> CDOT offset -> DATAPOOL block). Output a calibration file generated ON THE PHONE from its
   own acdb (vendor data never in the repo).
2. Kernel: q6asm/q6adm open with the topology the calibration names and send ASM/ADM SET_PP_PARAMS (and AFE set-param
   for speaker protection) from that file (request_firmware), per stream/device start. Upstream-shaped: a generic
   "calibration blob" path, not Duo code.
3. A/B by ear and with the mic measurement (with an AGC-free mic), then speaker protection (VI sense feedback from the
   WSA8815 amp) last - it needs the AFE feedback path too.

## Boot "fart" from the speaker (the tester 2026-11-07: right after the console text, before the GUI) - analysis 2026-11-08
Kernel log of a pmOS boot (no sound played for this): card bind 27.5 s (silent, amp already awake via
power/control=on); phoc ~33 s, PipeWire/WirePlumber ~38 s; **40.23 s** first speaker open = q6core registers the
custom topologies + the COPP/AFE calibration is sent; **40.41 s** `MultiMedia1: no backend DAIs enabled` = WirePlumber
probing the PCM BEFORE UCM routing is applied; 40.71 s the SLIMbus satellite reports again (the real open). So the noise
comes from WirePlumber's first open/probe at session start, the first time the DSP runs our speaker chain - not from
the card bind or an ADSP restart. Candidate fixes (daytime, one listening A/B each):
1. UCM: turn the amp on LAST and off FIRST (SpkrLeft DAC Switch after the RX path, RX7 Digital Volume ramp from 0) so
   DSP start-up garbage never reaches the WSA.
2. Warm the DSP chain silently at boot: duo-hw plays 0.2 s of digital silence through the routed speaker path with
   the amp DAC switch off, so the first topology/calibration run happens muted.
3. WirePlumber: skip the unrouted probe (api.alsa.use-ucm already on; check `api.alsa.disable-batch`/probe options).
Test = cold boot to the GUI with the tester listening; compare against today's default.
