# Workbench — Speaker and microphones

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)). Text moved unchanged from leg 33 on 2026-11-05; newest log entries first.

## Log
- 2026-11-07 evening: **Independent check of the speaker tuning with the PC's BRIO webcam mic - INCONCLUSIVE.**
  duo-speaker-measure.py gained `--play` (sweep only) and `--analyze <wav>` (deconvolve an outside recording).
  The BRIO's mic has automatic gain control (peak ~0 dBFS at 0.25 AND at 0.08 sweep level) and the two runs differ by
  up to 7 dB at 400 Hz-1 kHz; it also looks band-limited near 8 kHz (both runs -27 dB from 8 kHz up = a 16 kHz speech
  path, not the speaker). Agreement of both runs: 1.3-6.4 kHz within +-3 dB through the tuned sink. Better tuning needs
  a measurement mic without AGC (any USB measurement mic, or a phone/recorder app with AGC off) or Android's ACDB
  EQ/MBDRC values. The current filter-chain (60-duo-speaker-tuning.conf) stays.
- 2026-11-07 15:35: **The boot "pop" is probably NOT the speaker (PARKED - the tester: "audio is fine we can fix that
  later").** Quiet-room BRIO recording of a full pmOS boot (floor -90 dBFS), synced to the phone's uptime: the login
  chime (1 s after PipeWire, 4-5 kHz bell bursts) and, before it, a separate LOW-FREQUENCY THUMP at uptime ~33.3-33.65 s
  (-7 dBFS, zero-crossing rate 400/s = ~200 Hz fundamental, big DC swings, decays in ~300 ms) - ~200 Hz is the haptic
  motor's resonance (DT: DA7280 at 190 Hz), and the phone's speaker path was not active yet (PipeWire 39.3 s). Started
  in that window: elogind 33.36, iio-sensor-proxy 33.41, polkit 33.43 (the da7280 driver probed at 23.2 s, so not its
  load). An iio-sensor-proxy restart test was inconclusive (one thump-like event after restart 1, none after restart 2,
  room got noisy). Next: `buzz /dev/input/event260 300` (static build in a local folder; tools/linux/buzz) while the BRIO
  records - if the motor's signature matches, find who touches the motor at ~33.5 s (elogind enumerating input/FF
  devices? the DA7280 waking on its I2C bus?).
- 2026-11-07 15:15: **The EALREADY burst is fixed too (kernel 00057).** Cause: Q6ASM_STREAM_STOPPED meant two things -
  after a STOP trigger the stream is still OPEN on the DSP (only an EOS was sent), after prepare's clean-up it was
  closed. prepare cleared the previous setup only in RUNNING, so PipeWire's stop -> re-prepare (no close) re-opened an
  open stream -> ADSP_EALREADY on every retry until the PCM was closed. Commit "ASoC: qcom: q6asm-dai: close a stopped
  stream before preparing it again": clear whenever not IDLE, and IDLE = nothing open on the DSP (what close() assumed).
  A/B with the same sequence (3 fast profile cycles + a recording): 00055 = 264 open failures / 181 DSP errors, capture
  stuck in SETUP; 00057 = 0 / 0, capture RUNNING; then 5 back-to-back record+play rounds all OK, 0 errors.
  The q6asm-dai capture path now has no known failure mode.
- 2026-11-07 14:35: **q6asm-dai DSP race: capture now recovers (kernel 00055).** Root cause in q6asm_dai_prepare(): a
  media-format failure returned with the DSP stream still OPEN, and the other error paths FREED the audio client while
  the PCM stayed open -> the next open of the session = ADSP_EALREADY ("DSP returned error[9]"), every later prepare
  "audio client freed", capture dead until a reboot. Commit "ASoC: qcom: q6asm-dai: leave a failed prepare retryable":
  every error path undoes routing / DSP stream / memory map in reverse order and keeps the client (close() frees it);
  the message names open_read vs open_write. Test (pmOS): 3 fast profile off->HiFi cycles -> no errors; right after the
  next capture closed, a 6 s burst of open_read EALREADY (PipeWire reopening while the DSP still finished the close),
  then 3 recordings in a row OK, no reboot. Open (smaller): that burst - check whether q6asm's CMD_CLOSE waits for the
  DSP's close response before the session id is reused.
- 2026-11-07 13:40: **No phantom headset + speaker pop hunt (OPEN: pop when Phosh appears).**
  * Phantom headset FIXED (kernel series 0053, one-shot verified on pmOS, 6.19.0-00054): the Duo has no jack, but
    wcd934x MBHC measured the floating HPH/MIC pins at every boot ("Impedance detect ramp error", "MONO plug type
    detected") and reported Headphone Jack + Headset Mic Jack = ON. New DT boolean `qcom,no-headset-jack` (codec node,
    binding documented) skips MBHC; set_jack then returns -ENOTSUPP, which sdm845.c accepts. Now both jacks = off,
    PipeWire active port = Speaker. The sdm845 machine driver still creates the (idle) jack controls for every board -
    left as is (a card-level property for a cosmetic control is not worth it). Leftover: the codec still parses the
    MBHC DT properties twice (wcd934x_init_dmic + codec_parse_data) -> one warning; skip it with the same property.
  * POP (the tester: "sound onload is still a popping noise"; "the pops are def from the phone"; heard "when Phosh
    appears"). Measured with the phone's own mics (1 cm from the speaker) and the PC's BRIO webcam mic:
    - no pop: every speaker mixer write (SpkrLeft DAC/BOOST/COMP/PA Volume, RX7 Digital Volume, COMP7) off->on,
      idle and while streaming (largest tick -33 dBFS on COMP off, vs -52 floor); sink SUSPENDED -> resume with
      silence; the WSA amp runtime-PM cycle; a full pipewire/wireplumber/pipewire-pulse restart (BRIO, quiet room).
    - pop: card profile off -> HiFi cycles (pactl set-card-profile) - a repeating pair 0.4 s apart (BRIO), heard by
      the tester; those cycles also hit the DSP race below, so they are not a clean trigger.
    - lead for next time: something Phosh's session does when it appears (an event/login sound via feedbackd/gsound,
      or a volume/mute change applied abruptly) - not PipeWire's start, which tested clean.
    - best hypothesis (13:55): the COLD transition. At boot the codec mixer path is off; the session's first HiFi verb
      enable (UCM EnableSequence: SLIM RX0 / RX INT7 / COMP7 routes, then Speaker: RX7 volume, SpkrLeft COMP/BOOST/DAC,
      PA volume) + the first PCM open is an off->on step = the same thing the profile off->HiFi cycles did (popped). A
      PipeWire restart does not pop because the mixer is already on. Test next: ONE slow profile off->HiFi cycle (10 s
      apart, never rapid = the q6asm race) with the BRIO in a quiet room; if it pops, reorder the Speaker
      EnableSequence (PA Volume 0 + DAC off first, enable BOOST/COMP, DAC, then ramp the PA volume) and re-measure.
      Event sounds are on (theme freedesktop, service-login.oga exists) - check whether phosh/feedbackd plays a
      login event in the same second.
  * DSP RACE (new bug): fast profile off/HiFi switching -> `q6asm_dai_prepare: private data null or audio client
    freed`, then every capture open fails with `q6asm_open_write failed` / DSP error 9 until a reboot (playback still
    ran). Never cycle the card profile quickly; recovery = reboot.
  * Measurement gotchas: pw-record without --target records the DEFAULT source = a sink MONITOR (all zeros); stop
    pw-record with `timeout -s INT` (killing su leaves a WAV with no frames); `amixer cget`'s LAST line is the dB scale
    (`...mute=0`), the value is on the `: values=` line; `pidof pipewire` also lists pipewire-pulse (same binary).
    Tools: tools/linux/audio-tuning/duo-pop-bisect.py (ON the phone: its mics vs each speaker mixer write; refuses to
    run unless the card is in the UCM speaker state), duo-transient-find.py + duo-transient-shape.py (PC, any mono WAV,
    e.g. the BRIO: `ffmpeg -f dshow -i audio="Microphone (Logitech BRIO)" -ac 1 -ar 48000 x.wav` -> transients on a
    clock, envelope + zero-crossing rate: a pop = low-ZCR thump decaying in tens of ms, typing = 3-5 kHz clicks).

- 2026-11-03 11:45: offline — Windows audio corrected: no WCD934x driver exists (gus, Guides#70); Windows side = new codec driver.
- 2026-11-03 12:20: offline — audio memory made precise (WCD SoundWire/WSA path is the missing part); Windows design options in leg 24 (codec-register path via RTAC first).
- 2026-11-04 12:50: **MICROPHONES WORK** (DTB #55 default): two fixes after round 2 — (1) mainline sdm845's BE fixup forces 2 channels, so feed TWO codec TX ports (TX7 = DEC7/DMIC1 handset mic, TX8 = DEC8/DMIC2) → the DSP's capture COPP opens, arecord runs; (2) the DMICs had no power: Microsoft's DT routes DMIC1←MIC BIAS1, DMIC2←MIC BIAS3 → `audio-routing` in sm8150-microsoft-surface-duo-mic.dtsi → quiet-room recording rms 989 (ch0) / 69 (ch1), DAPM MIC BIAS1/3 + DMIC1/2 On. Normal boot with init loading the card: fine.
- 2026-11-04 12:45: Microphones, round 2: sound card machine driver is now a MODULE (kernel #60, initramfs #64: init insmods snd-soc-rt5663 + snd-soc-sdm845; duo.nosound=1 skips) → normal boot verified. Mic test DTB (#54-mic, capture link) boots fine with duo.nosound=1 and the card then LOADS fine by hand (no hang; the earlier hang was the boot-time probe or a random early hang). arecord now opens but reads fail (I/O error): codec log "underflow error on TX port 7", all capture DAPM widgets Off with out 0 (DMIC1/DMIC MUX7/ADC MUX7/CDC_IF TX7/AIF1 CAP/SLIM TX7), DSP "q6adm cmd 0x10325 error 2" opening the capture COPP; and playback + capture on one FE (MultiMedia1) collide ("Audio Client already active"). Next: a MultiMedia2 capture FE, then the q6adm capture COPP error (topology/format).
- 2026-11-04 12:30: Microphones: Android handset-mic = DMIC1 → ADC MUX7/DEC7 → CDC_IF TX7 → SLIM TX7 (AIF1_CAP) → SLIMBUS_0_TX → MultiMedia1 (mixer_paths_tavil.xml on vendor); arecord needs a capture DAI link ("no backend DAIs enabled"). Added slim-capture-dai-link (SLIMBUS_0_TX → wcd9340 AIF1_CAP) = DTB #53 → its one-shot boot HUNG (no SSH; the tester's power button) — capture link or a random early hang, unknown; default stays DTB #52. Speaker tone heard by the tester ("beep"). Next: test the capture link with ramoops-free logging (serial/kmsg stream) or add it at runtime. New task (d2): fat-penguin splash logo.
- 2026-11-04 10:00: qcauddev has the WSA path compiled in: SoundWire AHB-bridge write/read (0x9a0a8/0x9a488) + a SWR master cluster; SD_N/ID-table ideas ruled out (leg 24 step 3).
- 2026-11-04 09:50: Windows audio IOCTLs (offline): private type 0xADAE; qcauddev only SENDS, qcadcm handles 78 codes (two ranges) = the codec-control hub (leg 24 step 2).
