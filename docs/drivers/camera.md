# Workbench — Cameras (CAMSS + IMX351)

Chapter of the Linux workbench notes (hub: [../33-linux-workbench.md](../33-linux-workbench.md)). Text moved unchanged from leg 33 on 2026-11-05; newest log entries first.

## Current understanding (2026-11-06)
- **Works, in the default boot** (kernel #79, DTB cam-axi, initramfs #91; off: `duo.nocamera=1`): Sony IMX351 (CCI0
  master 1, addr 0x1a) → CSIPHY1 (4 lanes) → CSID0 → VFE0 RDI0, RAW10 SBGGR. Sensor modes: 4032x2268 (mode 0) and
  1920x1080 2x2-binned ~30 fps (`imx351 mode=1`, what init loads). Controls: exposure, analogue gain (Sony law
  1024/(1024-code), 1x-22x), VBLANK; get_selection reports the 4656x3496 array + per-mode crop.
- **The fix that made frames land:** the GCC camera AXI clocks (HF + SF) were never enabled on SM8150
  (patches/camss-sm8150-gcc-axi.py + the SF clock in dts/...-camss.dtsi). Before it, the VFE "completed" buffers that
  stayed untouched.
- **Sensor programming:** Android's exact init + mode writes, captured from Droidian's CCI trace and turned into a
  header locally (camera/cci-trace-header.py; vendor data, never committed). Build: drivers/imx351/build-oot.sh.
- **Userspace (updated 2026-11-06):** libcamera 0.5.1 + OUR IMX351 helper/properties/FrameDurationLimits patch
  (camera/libcamera-imx351.py, built by the tester's camera/duo-libcamera-build.sh -> (local file, not published))
  -> PipeWire/WirePlumber -> **GNOME Snapshot works** (photos + video UI) in Phosh. Install order matters: the
  libcamera-imx351 overlay LAST (another overlay brought the stock libcamera back: "Failed to create camera sensor
  helper", green picture). Tuning: camera/imx351.yaml (AWB, no CCM - a borrowed CCM made it far worse).
  Older tools: `camera/duo-libcamera-test.sh`, `camera/duo-camera-preview.sh` (kmssink), our small duo-camera-app.py.
- **Flash LED (2026-11-05):** PM8150L flash module, mainline leds-qcom-flash; dts/duo-flash-overlay.dts (rear flash =
  channel 1, torch 135 mA, flash 500 mA) -> `white:flash`; torch + strobe verified. Not in the default DTB yet.
- **Autofocus (2026-11-05):** DW9800L at 0x0c via mainline dw9719 (out of tree), shared module regulator on gpio11;
  focus_absolute 0..1023 reaches the chip. Test DTB only (test-dtb-cam-af).
- **FIXED 2026-11-06: the dark/green picture was the exposure CAP, not colour tuning.** Mode 1 ran a 2092-line frame (9.1 ms, ~109 fps), so AE could expose at most ~9 ms (libcamera never touches VBLANK); FLL 7624 = 30 fps (imx351.c) -> raw signal 3.9x (17 -> 68 over black at 22x), and through libcamera R/G/B 9.8/38.6/16.4 -> 72.5/79.5/79.1 (G/R 1.10, G/B 1.00) on the same scene. Older measurement notes: Measured (camera/rgb-means.py, camera/duo-raw-gain-test.sh): white paper in a
  lit room = R 9.8 / G 38.6 / B 16.4 with libcamera's AE at exposure + gain maximum; raw frames: gain and exposure both
  act linearly but the signal is ~1000x below what a lit sheet should give. So it is light starvation, not colour
  tuning: next suspects = the Android init table putting the sensor into a short-exposure (HDR/QBC) mode, mode 0 vs
  mode 1, the lens/shutter. libcamera's soft AWB clamps gains at 4x, so starved red stays low. Also: libcamera 0.5.1
  FATALs in PipelineHandler::stop() with queued requests (PipeWire's camera then needs a restart).
- **Ruled out on the way** (see log): BOB/vana supply, CAMNOC QoS/UBWC, WM programming/order, VFE instance, IOMMU
  faults - the sensor readout window and then the AXI clocks were the real causes.
- **Gotchas:** never devmem the apps SMMU (0x15000000; hard hang). Unloading imx351 prints a runtime-PM warning
  (harmless). Photos show the room: keep them in (local file, not published) never commit.

- **AUTOFOCUS WORKS (2026-11-06 23:56, pmOS):** the lens had sat at focus 0 since day one (libcamera's simple
  pipeline has no AF). Torch-lit sweep, can at ~15 cm (gradient energy, centre crop): 13.9 at 0 ... 59.8 at 512,
  **103.2 at 576**, 78.7 at 640, 22 at 768 -> the DW9800L moves the lens and close-ups were badly defocused.
  `tools/linux/camera/duo-autofocus.py` (Phosh autostart): a 2nd PipeWire reader of the camera stream (works beside
  Snapshot: one source node, two links), 320x180 grey, 2x2-binned gradient score (dim-room noise), coarse 0..1023/64
  then fine +-64/16, lock, 5-frame baseline as the reference, refocus only on a lasting change, KEEP 15 % hysteresis,
  flat-sweep guard. Dim room, no torch: lock at 576 in 4.7 s, stable. Lens control = VIDIOC_S_CTRL on the dw9719 subdev
  (an open fd keeps the actuator powered).
- **pmOS was missing the soft IPA (2026-11-06):** only libcamera.so of our overlay had been copied; without
  ipa_soft_simple.so there is no AE/AWB (max exposure + gain, dark cyan). duo-pmos-prepare.sh now unpacks the whole
  overlay and md5-checks the IPA. `cam` needs libevent (added to the package list).
- **Dim light (2026-11-06):** white paper, lamp light, 30 fps: R/G/B 26/44/45 (cyan = starved signal + the 4x AWB
  clamp, not swapped channels); VBLANK 14168 (15 fps, 66 ms max exposure): 52/63/65. Auto 15/30 fps switching = TODO.
- **GPU SOFT ISP ONLINE (2026-11-07 00:17): libcamera 0.7.2 + DebayerEGL on the Adreno 640** (GL_RENDERER FD640, Mesa
  25.1 freedreno, GLES 3.2). camera/duo-libcamera-build-pmos.sh builds it on the phone (on disk) with our IMX351 patch
  (patch 4 = proportional AE is upstream in 0.7) AND PipeWire 1.4.7's libspa-libcamera.so against it (0.7 = new soname;
  two API fixes: Camera model string_view, FrameBuffer::planes() Span). Backup of the 0.5 stack:
  /var/tmp/libcamera-0.5-backup.tar.gz on the phone. Tuning file for 0.7: camera/imx351-0.7.yaml ('Lut' -> 'Adjust').
  A/B (1080p XRGB8888, frames / elapsed time from cam's timestamps, no skipped sensor frames; LIBCAMERA_SOFTISP_MODE):
  30 fps timing: GPU 32.9 fps at 8.5 % CPU vs CPU 32.9 at 13.8 %; Android's 1080p table (VBLANK 1012): GPU 120 vs CPU 71;
  minimum blanking (VBLANK 20, 1100 lines): **GPU 228 fps = the sensor limit** vs CPU 74 (0.5's CPU path reached 174).
  Snapshot works on it (two PipeWire readers: Snapshot + autofocus). Note: cam's per-frame "(x fps)" numbers are
  instantaneous and mislead (a first A/B read 40 / 11 fps) - always use timestamps. 4x "eglCreateImageKHR fail" per
  stream = the packed RAW10 input is uploaded instead of imported (zero-copy input = a possible next speed-up).
- **Gap to Android (honest):** Android uses the Spectra hardware ISP with Microsoft's tuning (LSC, denoise, tone
  map, sharpening, CCM, HDR). Mainline has only RDI raw capture, so all of that is software work: AF (done), tuning
  (CCM/black level/LSC/gamma in daylight), auto frame rate, newer libcamera (GPU soft ISP), denoise/sharpen.

## Log
- 2026-11-08 17:55: **Camera on Debian (trixie).** Stock libcamera 0.4.0 streamed the IMX351 (simple pipeline, CPU
  soft ISP, 33 fps) but dark green - no IMX351 helper, no LSC/CCM. `tools/linux/debian/duo-libcamera-build-debian.sh`
  (run by Claude at the tester's word "You run it"): libcamera master + our IMX351 patch -> /usr/local (libcamera.so.0.8),
  PipeWire 1.4.2's libcamera plugin with the frame-rate patch, imx351.yaml tuning copied from pmOS. First run failed
  at apt: libgbm-dev from trixie (25.0.7) vs Mesa 26 from backports -> pinned to the installed libgbm1. Result: real
  colour, good exposure in a dim room, 30 fps (1080p BGR888). Note: patch 4 (proportional AE) skipped - upstream AE
  changed since 0.5.

- 2026-11-07 11:31: **FULL-SCREEN 4:3 PREVIEW.** Snapshot 48's Aperture picks the largest "exact 16:9" size with an f32
  test `width == (height*16/9) as i32` - 1080*16/9 truncates to 1919, so 1920x1080 never qualified and Snapshot streamed
  1280x720. Our patch (camera/snapshot-preferred-ratio.py, built on the phone by duo-snapshot-build-pmos.sh, installed as
  /usr/libexec/snapshot.duo behind the /usr/bin/snapshot wrapper; original kept as snapshot.duo-orig): f64 + round(),
  SNAPSHOT_PREFERRED_RATIO (4:3 on the Duo = the 3:4 screen upright) and SNAPSHOT_WIDTH_ALIGN=64 - the GPU soft ISP pads
  RGBA rows to 256 bytes (1400 px -> 5632) and the GTK viewfinder sink ignores the padded stride (streaks; a plain
  GStreamer pipeline with the same buffers is clean). Result: 1152x864, upright, fills the portrait screen.
  Upstream candidates (drafts for the tester): the rounding fix (Snapshot), the stride bug (gtk4 paintable sink path).

- 2026-11-07 09:30-10:00 (the tester testing Snapshot live): **rotation = 90** (DT `rotation`; 270 was upside down in
  portrait - my laptop-pose derivation was 180 deg off; libcamera then rotates for portrait). **AF**: full 26-step sweep
  every scene change = constant clicking -> local search after 0.7 s steady (MIN_GAP 2 s, full sweep <= every 10 s),
  time-based lens settle (200 ms), sharpness at 1280x720 (peak 51 vs 28 instead of 363 vs 272 at 320x180). **Tiny
  icons** = phoc global scale-to-fit (now per app). **Preview not filling the 3:4 screen (OPEN)**: Snapshot streams
  1280x720 (16:9); the screen is 3:4 = the sensor's 4:3 rotated (Android's framing). 4:3 at 30 fps works (cam:
  1280x960 33 fps, 1600x1200 30 fps). Snapshot 48's Aperture prefers 16:9 (utils caps: OPTIMAL_RATIO 16/9, MAX_HEIGHT
  1080) and selects per frame rate; PipeWire 1.4.7's libcamera SPA listed NO frame rate -> new
  camera/pipewire-libcamera-framerate.py (fixed 30/1 from FrameDurationLimits; installed, backup
  libspa-libcamera.so.pre-framerate). Snapshot STILL negotiates 1280x720 -> next suspect: Aperture's SUPPORTED_CAPS
  formats exclude RGBA (our GPU ISP's only output) so its selection comes out empty. Next: read Aperture 48.0.1
  source (lib.rs SUPPORTED_CAPS) and patch it (4:3 preference + RGBA) in a Snapshot rebuild on the phone. Never run
  `cam` while PipeWire holds the camera: it took down pipewire + wireplumber (audio gone until restarted).

- 2026-11-05 19:20: **AUTOFOCUS DRIVER WORKS (lens control path).** Shared module regulator on gpio11 (regulator-fixed) as imx351 `vana-supply` (driver: optional vana regulator, power-gpios now optional) and lens `vdd-supply`; lens@c "dongwoon,dw9800k" + sensor `lens-focus`. Test DTB (local file, not published) (378aa1b5) + modules imx351 (vana) + dw9719 (out of tree, CONFIG_VIDEO_DW9719 is off in our .config): dw9719 6-000c registers a subdev with focus_absolute 0..1023; with the camera streaming, focus_absolute 100/512/900 lands in the chip as 0x064/0x200/0x384. NOTE: this DTB drops power-gpios, so it needs the new imx351.ko (the old one fails "no power gpio"). Open: a lit-scene sharpness sweep to prove the lens moves; default = DTB + kernel (DW9719=m) + initramfs with both modules. dts/...-imx351.dtsi now carries the regulator + lens nodes.
- 2026-11-05 19:05: lens DT test (fdtput copy (local file, not published): lens@c "dongwoon,dw9800k" + sensor `lens-focus`): dw9719 probes but its power-up write fails (reg 0x02: -ENXIO) - the actuator is unpowered at probe because its 2.8 V comes through the sensor power switch (gpio11), which only the imx351 driver drives while streaming; with the lens unbound the sensor sub-notifier never completes, so CAMSS shows no subdevs (camera unusable on that boot - default boot unaffected). Fix plan: a regulator-fixed on gpio11 (enable-active-high) as the shared supply - imx351 takes it as an optional `vana` supply instead of power-gpios, the lens as `vdd-supply` - so either driver can power the module.
- 2026-11-05 19:00: **AUTOFOCUS actuator identified + answering: Dongwoon DW9800L** (Semco module; the phone's own CamX
  module config names "dw9800l_actuator"; Android DT: qcom,actuator on CCI master 1, cam_vaf = fixed 2.8 V from
  PM8150L BOB - no own enable pin, so it is powered with the sensor). On i2c-6 (CCI0 m1) at 0x0c while the camera
  streams: reg 0x00 = 0xEB (ID byte; differs from the dw9719 driver's known IDs), 0x03/0x04 = 10-bit lens position -
  writes 0x1ff / 0x040 / 0x320 read back exactly, status 0x05 = 0 (idle). Mainline 6.19 drivers/media/i2c/dw9719.c
  has a DW9800K mode ("dongwoon,dw9800k"); built out of tree it binds (instantiated by hand) but makes no control node
  until the sensor DT node links it (`lens-focus = <&lens>`). Next: DT lens@c node + lens-focus link, check that the
  DW9800K init (SAC mode / VCM frequency) suits the DW9800L, then a daylight sharpness sweep (lens 0..1023) to prove the
  lens moves; libcamera's simple pipeline has no AF algorithm - a manual focus control or a small contrast-AF script.
- 2026-11-05 18:45: imx351 driver parses the DT orientation/rotation (v4l2_fwnode_device_parse + v4l2_ctrl_new_fwnode_properties); DT camera node gets `orientation = <0>` (front; the Duo's single camera, Android uses it as both front and rear by folding). Tested on a one-shot DTB (fdtput copy (local file, not published)) + the module swapped live: camera_orientation = Front, libcamera's "Failed to retrieve the camera location" warning gone. Rotation still unknown (needs Droidian `dumpsys media.camera` or decoding the camx CameraInfo mountAngle). Not in the default yet (needs DTB + initramfs rebuild).
- 2026-11-05 23:05: **"Camera is green" = almost no light reaches the sensor, not a colour bug.** White paper in a lit
  room through Snapshot: R 9.8 / G 38.6 / B 16.4 (of 255) with libcamera's AE at exposure 2072 + gain 978 (both max).
  libcamera 0.5.1's soft AWB = sumG/sumR after black subtraction, clamped at 4.0 -> red pinned at 4x, still green.
  Raw test (`camera/duo-raw-gain-test.sh`, no libcamera, mode=1 1920x1080, black 64): exposure 2072 + gain 1x = 63.9,
  2.6x = 64.3, 22x = 78.4; at 22x exposure 200 / 1000 / 2072 = +0.5 / +7.9 / +17.8 over black. Gain and exposure both
  work (linear), but the signal is ~1000x below what a lit white sheet should give. Suspects: the camera not facing the
  paper / very dim light; the CamX init table putting the sensor in a mode where 0x0202 is a short (HDR/QBC) exposure;
  lens/shutter. Next: daylight raw test, mode 0 vs mode 1, compare the Android register trace for HDR/exposure regs.
  Also: libcamera 0.5.1 FATAL "queuedRequests_.empty()" in PipelineHandler::stop() inside PipeWire when a capture stops
  -> PipeWire's camera dies until pipewire + wireplumber restart.
- 2026-11-05 22:50: **CAMERA FLASH LED WORKS** (the tester saw it). The Duo 1's High CRI LED flash is on the PM8150L flash
  module (mainline `leds-qcom-flash`, already =m in our config; pm8150l.dtsi has `led-controller@d300`, disabled).
  Android DT (dtbo dt31): rear camera-flash -> flash_0 (id 0, 1200 mA, 1280 ms) + torch_0 (id 0, 135 mA). Our
  `dts/duo-flash-overlay.dts` (target-path, merged by `dts/build-flash-dtb.sh` with fdtoverlay onto the default DTB ->
  (local file, not published) 496c4817): led-sources <1>, torch 135 mA, flash 500 mA. One-shot boot via
  `DTB=test-dtb-cam-flash.dtb duo-grub-test.sh ""`, insmod led-class-flash + leds-qcom-flash -> `/sys/class/leds/white:flash`:
  torch at 63/255 for 3 s = lit; strobe 300 mA / 200 ms -> fired, fault "flash-timeout-exceeded" = the normal timer end.
  Next: make it default (DTB + modules in the initramfs), then a torch toggle in Phosh / flash in the camera app.
  Camera colour (same day, rgb-means.py): dim room G/R 1.97 G/B 2.78 (AWB inactive on a too-dark frame); lit room
  G/R 1.62 G/B 0.97 (AWB active, red still low); the borrowed CCM made it much worse and was removed.
- 2026-11-05 18:30: **libcamera + IMX351 helper, signed, in-process.** Second build (openssl in the overlay) ships ipa_soft_simple.so + its own .sign; libcamera loads the IPA in-process ("initializing soft proxy: loading IPA", no isolated worker), gain 1-22.26x. Use: extract (local file, not published) AFTER the stock libcamera overlay. Daylight check of colour/AE still to do.
- 2026-11-05 18:25: **libcamera with the IMX351 helper built (the tester's run).** The helper is active: libcamera now
  reports gain 1-22.26x (real gain, Sony law) and the "Failed to create camera sensor helper" warning is gone. Two
  findings: (1) the build had no openssl, so ipa_soft_simple.so was not signed and Alpine's stale .sign stayed next to
  it -> signature check fails -> the IPA runs in the isolated worker; openssl is now in the build overlay (re-run the
  build). (2) Evening test, camera facing a dark corner: both stock and our libcamera give near-black frames (means
  0.7 and 0.2 of 255; raw frames sit just above black level even at max exposure + 22x gain), so the light, not the
  build, made them black - judge colour/AE in daylight. Remaining libcamera warnings: no camera location/rotation
  controls (driver: v4l2_ctrl_new_fwnode_properties + DT orientation/rotation) and no HBLANK-writable "recommended"
  control. Stock libcamera restored on the phone for now.
- 2026-11-05 10:20: imx351 get_selection (driver): native 4656x3496 (16 MP array; readout X 0..4655 / Y 612..2879 + digital
  crop 312 per side from the Android mode registers), crop 312,612 4032x2268 (mode 0) / 408,666 3840x2160 (mode=1). libcamera's
  "Unable to get rectangle" errors are gone (tested by swapping the module live; the default initramfs #89 still has the old
  imx351.ko - picked up at the next initramfs rebuild). Left: libcamera has no IMX351 sensor helper (soft AE treats the gain
  code as linear; Sony gain = 1024 / (1024 - code), max code 978 = 22x) and no sensor delays -> a libcamera patch.
- 2026-11-05 10:05: **Daylight check + default-boot candidate.** The rotated full-panel preview (VB unset and VB=6284)
  in daylight: a plain wall, frame means RGB 76/82/79 (not clipped), slight green tint + dark corners (lens shading, no
  correction in the soft ISP yet). So the VB knob is a dim-room aid only - daylight needs none. Making the camera part
  of the default: kernel #79 = #78 + `CONFIG_CMA_SIZE_MBYTES=256` (no `cma=` on the cmdline), init loads the camera
  modules (lib/duo/camera, imx351 mode=1) when the DT has camss@acb3000 (off: duo.nocamera=1), imx351.ko built by
  tools/linux/drivers/imx351/build-oot.sh, DTB = test-dtb-cam-axi (= the default DTB + CCI/IMX351/CAMSS nodes only).
  One-shot first.
- 2026-11-05 09:50: **Colour: the green/dark libcamera picture is a light problem, not a wrong pipeline.** Same scene,
  same exposure 2072 / gain 978: the raw frame through raw10-to-png.py is a neutral white wall only because that tool
  stretches - the raw means after black are just 20.6/31.2/15.6 of 1023 (~3 % of full scale, G:R:B = 1:0.66:0.5), and the
  soft ISP shows that level honestly (dark, AWB unreliable on noise -> green). libcamera's soft AE never lengthens the
  frame, so it sits at exposure = frame length + max gain. With VBLANK 6284 (frame length 7364, set on the sensor
  subdev before `cam`): AE took exposure 7344 (3.5x) -> clearly brighter, near-neutral grey. Both camera scripts take
  VB=<lines>. Real fix later: an AE that also drives VBLANK (or libcamera's camera sensor helper for IMX351 + frame
  duration limits), plus a CCM/AWB tuning in daylight.
- 2026-11-05 09:45: **Portrait full-screen preview.** `videoflip method=clockwise ! videoscale ! 1350x1800 ! kmssink`
  (videoflip needs gst-plugins-good: (local file, not published)) fills the top panel with the rotated live
  image. An aspect-correct 1016x1800 plane failed: drmModeSetPlane ENOSPC = the DPU has no free pipes for that plane
  next to the console's (each panel runs two layer mixers for its 2 DSC slices); the exact panel size works. CPU load
  stays low (software debayer + flip + scale at 30 fps on the A76 cores). duo-camera-preview.sh updated (ROT=1 default).
- 2026-11-05 09:40: **LIVE CAMERA PREVIEW on the Duo's panel.** gst overlay (gstreamer-tools, libcamera-gstreamer,
  gst-plugins-base, gst-plugins-bad; 139 MB, RAM only) + tools/linux/camera/duo-camera-preview.sh:
  `libcamerasrc ! videoconvert ! videoscale ! video/x-raw,width=1350,height=760 ! kmssink connector-id=34` -> the camera
  image (the wall, teal from the untuned AWB) on a DRM overlay plane over the console of the top panel (webcam photo).
  Caps note: the soft ISP outputs 1912x1080 (fixed caps 1920x1080 -> not-negotiated). Open: full-screen + rotation for
  the portrait panel, colour (CCM/AWB tuning, IMX351 sensor helper for libcamera), longer exposure in low light,
  and the default boot (DTB + modules + cma=256M + kernel #78).
- 2026-11-05 09:35: **libcamera + soft ISP produce RGB frames.** Kernel #78 (build #83; Image d2e8f854) = #76 config +
  DMABUF_HEAPS(+SYSTEM, CMA) + UDMABUF (build-kernel.sh sets them now); boot with `cma=256M` (the default CMA pool is
  48 MB and ran dry at the soft ISP's third 8 MB frame). tools/linux/camera/duo-libcamera-test.sh: `cam -c 1
  -s pixelformat=BGR888 --capture=N --file=/tmp/lc-#.ppm` -> 1912x1080 BGR888 frames (6194880 bytes each) through the
  simple pipeline + software ISP. Tuning: tools/linux/camera/libcamera-imx351.yaml (install as
  /usr/share/libcamera/ipa/simple/imx351.yaml; uncalibrated + black level 4096 = 64 @ 10 bit). The pictures are right
  but dark/green in the dim room: the AE already runs exposure 2072 (= the 1080p frame) and gain 978 (~22x) - it needs
  longer frames (lower fps via VBLANK), which libcamera's soft AE does not change; green = no CCM/AWB tuning yet
  ("Failed to create camera sensor helper for imx351": libcamera has no IMX351 helper). `cam -D` (KMS viewfinder on
  DSI-1) aborts on an unset std::optional<ColorSpace> (soft ISP stream colour space "Unset") - a cam tool bug; next:
  a GStreamer preview (libcamerasrc ! kmssink) or our own small viewer.
- 2026-11-05 09:30: **libcamera sees the camera.** Overlay: `OVERLAY_BASE="libcamera libcamera-tools libcamera-ipa"
  make-dev-overlay.sh` -> (local file, not published) (Alpine 3.22: libcamera 0.5.1, `cam`, ipa_soft_simple).
  `cam -l` found nothing until udevd ran (libcamera enumerates via libudev; udev overlay: `udevd --daemon; udevadm
  trigger --subsystem-match=media --subsystem-match=video4linux; udevadm settle`) -> `1: 'imx351'
  (/base/soc@0/cci@ac4a000/i2c-bus@1/camera@1a)` through the simple pipeline handler. Its software ISP (debayer + AE/AWB)
  then fails "Failed to create DmaBufAllocator": the kernel has no DMA-BUF heaps / udmabuf -> kernel #78 = #76 config +
  DMABUF_HEAPS, DMABUF_HEAPS_SYSTEM, DMABUF_HEAPS_CMA, UDMABUF (building).
- 2026-11-05 09:25: **Sensor controls for AE (imx351).** Added the standard controls libcamera's simple pipeline +
  soft ISP need: EXPOSURE (0x0202, max frame length - 20), ANALOGUE_GAIN (0x0204 code 0..978, gain = 1024/(1024-code)),
  VBLANK (frame length 0x0340 = height + vblank, updates the exposure range) and a read-only HBLANK (line length -
  width); applied at stream on via __v4l2_ctrl_handler_setup and live while streaming. Per-mode defaults: full mode =
  the Droidian trace (FLL 5068, LLP 5544, exposure 5048, gain 626), 1080p = camx table (FLL 2092, LLP 2896, exposure
  2072, gain 0). Verified (1080p): gain 0 -> raw green mean 2.1, gain 600 -> 5.3 (x2.5; formula predicts x2.41).
  Next: libcamera (Alpine package, simple pipeline + soft ISP) -> `cam` capture with auto exposure/white balance.
- 2026-11-05 09:20: **1080p mode works (imx351 `mode=1`).** The camx tables hold two 1920x1080 2x2-binned modes (tables
  0x61168 ~30 fps FLL 2092, 0x64350 ~60 fps FLL 1152); compared with the full mode only PLL_MPY 0x0307, frame/line
  length, exposure, binning 0x0900/01 = 01/22 and the output / digital-crop sizes differ - the CSI link rate is the
  same (so the link-freq/pixel-rate controls stay). drivers/imx351: mode=1 writes the trace's full mode then those
  overrides + a centred digital crop (x 48, y 27 inside the binned 2016x1134). Result: 30/30 frames of 2592000 bytes
  (1920x1080x10 bit) written, a correct picture of the same scene (darker: shorter exposure; AE is next).
  Build note: imx351-camx-regs.h is generated locally ((local file, not published) from droidian-cci-writes.txt with
  camera/cci-trace-header.py) and the module builds out of tree against /root/linux; duo-camera-marker-test.sh takes
  MODE=1. Glitch seen once: an SSH drop while pulling a frame - USB, not a reset.
- 2026-11-05 09:15: **CAMERA WORKS - first real frames on mainline (step 12).** The missing piece were the GCC camera
  AXI clocks: sdm845's VFE resources (which SM8150 reused) list neither, so nothing in camss ever enabled
  GCC_CAMERA_HF_AXI_CLK / GCC_CAMERA_SF_AXI_CLK (both enable 0) - the camera NoC's path to DDR was unclocked, the VFE
  still "completed" every frame and no byte landed, with no fault. Research found it: downstream's SM8150 CPAS enables
  gcc_axi_hf_clk + gcc_axi_sf_clk (Droidian DTB), and mainline's sc8280xp (VFE 175-200) lists gcc_axi_hf/gcc_axi_sf first.
  Fix: patches/camss-sm8150-gcc-axi.py (SM8150 gets its own VFE table = sdm845's + "gcc_camera_axi" + "gcc_camera_sf_axi")
  + dts/...-camss.dtsi adds GCC_CAMERA_SF_AXI_CLK. Test (kernel #76 + initramfs #88 + (local file, not published)
  = venus+usbc+camera+camss+imx351; modules (local file, not published); tools/linux/camera/duo-camera-marker-test.sh,
  reconstructed from the 2026-11-04 commands so it is in the repo now): marker test 10 frames, ~178300 of ~178600 sampled
  bytes overwritten per frame (before: every byte still 0xaa); frame 4032x2268 SBGGR10 -> raw10-to-png.py -> a real,
  correctly coloured photo (ceiling + shelf; kept local only, privacy rule). Next: make it usable - lower-resolution
  sensor modes, a preview on the panel, auto exposure/white balance (libcamera simple pipeline / soft ISP), then the
  default DTB + init loading the camera modules.


Older entries (2026-11-04 bring-up): [camera-history.md](camera-history.md).
