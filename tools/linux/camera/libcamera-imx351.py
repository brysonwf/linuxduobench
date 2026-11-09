#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Add the Surface Duo's Sony IMX351 to a libcamera source tree (tested shape: libcamera 0.5.x; docs/research/workbench/camera.md):
#  - src/ipa/libipa/camera_sensor_helper.cpp: a helper so the soft ISP's AE converts gain codes correctly. Sony's law is
#    gain = 1024 / (1024 - code) (our driver's ANALOGUE_GAIN 0..978 = 1x..22x); without a helper libcamera assumed linear.
#    Black level 64 @ 10 bit (= 4096), as for the IMX355 (same family; our tuning file uses it too).
#  - src/libcamera/sensor/camera_sensor_properties.cpp: unit cell 1.0 um (IMX351 = 16 MP 1/3.06"; unverified against a
#    datasheet), sensor delays 2/2/2/2 like the IMX355 (the driver writes exposure/gain/VBLANK directly, no group hold).
# Inserted right after the imx335 entries (alphabetical; libcamera 0.5.1 has no imx355 yet - upstream master added it),
# so it does not depend on exact line numbers. Verified on v0.5.1 source (applies once, second run = "already patched").
# usage: python3 -I libcamera-imx351.py <libcamera-src>
import sys, re

src = sys.argv[1]

def patch(path, anchor_re, block, done_marker):
    p = f"{src}/{path}"
    import os
    if not os.path.exists(p) and "/ipa/simple/" in p:      # libcamera master (post 0.7): src/ipa/simple -> softisp
        p = p.replace("/ipa/simple/soft_simple.cpp", "/ipa/softisp/softisp.cpp").replace("/ipa/simple/", "/ipa/softisp/")
    s = open(p, encoding="utf-8").read()
    if done_marker in s:
        print(f"{path}: already patched"); return
    m = re.search(anchor_re, s, re.S)
    if not m:
        sys.exit(f"{path}: imx335 anchor not found - libcamera layout changed, patch by hand")
    s = s[:m.end()] + block + s[m.end():]
    open(p, "w", encoding="utf-8").write(s)
    print(f"{path}: imx351 added")

patch("src/ipa/libipa/camera_sensor_helper.cpp",
      r'REGISTER_CAMERA_SENSOR_HELPER\("imx335", CameraSensorHelperImx335\)\n',
      '''
class CameraSensorHelperImx351 : public CameraSensorHelper
{
public:
	CameraSensorHelperImx351()
	{
		/* Surface Duo IMX351: 64 at 10 bits; Sony gain = 1024 / (1024 - code). */
		blackLevel_ = 4096;
		gain_ = AnalogueGainLinear{ 0, 1024, -1, 1024 };
	}
};
REGISTER_CAMERA_SENSOR_HELPER("imx351", CameraSensorHelperImx351)
''', "CameraSensorHelperImx351")

patch("src/libcamera/sensor/camera_sensor_properties.cpp",
      r'\{ "imx335", \{.*?\n\t\t\} \},\n',
      '''		{ "imx351", {
			.unitCellSize = { 1000, 1000 },
			.testPatternModes = {},
			.sensorDelays = {
				.exposureDelay = 2,
				.gainDelay = 2,
				.vblankDelay = 2,
				.hblankDelay = 2
			},
		} },
''', '"imx351"')

# 3) Soft IPA: advertise FrameDurationLimits (2026-11-05, Phosh/Snapshot test). 0.5.1's soft IPA publishes only Contrast, so
#    PipeWire's libcamera SPA offers caps WITHOUT a framerate (pipewiresrc: framerate=0/1) and GNOME Snapshot (Aperture)
#    never starts its camera. The range comes from the sensor timing the IPA already receives (mojom formula:
#    frameDuration = frameLength * lineLength / pixelRate); advertised only - the IPA does not act on the control.
#    libcamera master (post 0.7) dropped libcamera::Span for C++20 std::span (no include/libcamera/base/span.h).
import os as _os
SPAN = "Span" if _os.path.exists(f"{src}/include/libcamera/base/span.h") else "std::span"
patch("src/ipa/simple/soft_simple.cpp",
      r'\tControlInfoMap::Map ctrlMap = context_\.ctrlMap;\n',
      '''	if (sensorInfo.pixelRate) {
		/* FrameDurationLimits (us) so clients see a frame-rate range (Surface Duo patch) */
		int64_t minFD = static_cast<int64_t>(sensorInfo.minFrameLength) * sensorInfo.minLineLength *
				1000000 / sensorInfo.pixelRate;
		int64_t maxFD = static_cast<int64_t>(sensorInfo.maxFrameLength) * sensorInfo.maxLineLength *
				1000000 / sensorInfo.pixelRate;
		int64_t fdDef[2] = { minFD, maxFD };
		ctrlMap[&controls::FrameDurationLimits] =
			ControlInfo(minFD, maxFD, ControlValue(minFD));	/* SCALAR default: libcamera 0.8's ControlValue::get
							 * asserts !isArray_ - PipeWire reads def() as one int64 (WirePlumber
							 * crashed with the old SPAN<const int64_t, 2>{ fdDef } array default) */
		(void)fdDef;
	}
'''.replace("SPAN<", SPAN + "<"), "Surface Duo patch")
# master's softisp.cpp no longer includes span.h (0.7's soft_simple.cpp got it indirectly) -> include it explicitly
if SPAN == "Span":
    patch("src/ipa/simple/soft_simple.cpp", r'\n#include <[^\n]*>\n', '#include <libcamera/base/span.h>\n',
          "#include <libcamera/base/span.h>")

# 4) Soft IPA AE: PROPORTIONAL steps (2026-11-06, request: "the camera is still slow to adjust"). 0.5.1 moves exposure or
#    gain by a fixed 10 % per update whatever the error: dark -> bright room = ~30 exposure + ~30 gain steps (~66), at the
#    soft ISP's frame rate several seconds. Now one step = (optimal / measured MSV)^0.7, clamped to x0.5..x2 per update:
#    exposure first, then gain, when too dark; gain first, then exposure, when too bright. Inside the satisfactory band
#    nothing changes (no hunting).
import os
AGC = f"{src}/src/ipa/simple/algorithms/agc.cpp"
if not os.path.exists(AGC): AGC = AGC.replace("/ipa/simple/", "/ipa/softisp/")
_agc = open(AGC, encoding="utf-8").read()
if "kExpProportionalGain" in _agc or "\tdouble next;\n\tint32_t &exposure = frameContext.sensor.exposure;" not in _agc:
    # 0.7.x: proportional AE upstream; master (post 0.7): AE rewritten - our 0.5-era patch does not apply / not needed
    print("src/ipa/*/algorithms/agc.cpp: upstream AE differs from 0.5 - patch 4 skipped")
elif False:
    # libcamera 0.7.x has proportional AE upstream (factor = 1 + clamp(error * kExpProportionalGain)) - same idea
    print("src/ipa/simple/algorithms/agc.cpp: upstream already proportional - patch 4 skipped")
else:
  patch("src/ipa/simple/algorithms/agc.cpp",
      r'\tdouble next;\n\tint32_t &exposure = frameContext\.sensor\.exposure;\n\tdouble &again = frameContext\.sensor\.gain;\n',
      '''
	/* Surface Duo patch: proportional AE (replaces the fixed 10 % step below, which is skipped) */
	{
		const auto &cfg = context.configuration.agc;
		if (exposureMSV > kExposureOptimal - kExposureSatisfactory &&
		    exposureMSV < kExposureOptimal + kExposureSatisfactory)
			return;
		double f = std::pow(kExposureOptimal / std::max(exposureMSV, 0.05), 0.7);
		f = std::clamp(f, 0.5, 2.0);
		double total = static_cast<double>(exposure) * again * f;
		if (f > 1.0) {
			double e = std::min<double>(total / again, cfg.exposureMax);
			exposure = std::max<int32_t>(static_cast<int32_t>(e), exposure + 1);
			if (exposure >= cfg.exposureMax) {
				exposure = cfg.exposureMax;
				again = std::max(total / exposure, again + cfg.againMinStep);
			}
		} else if (again > cfg.againMin) {
			double g = total / exposure;
			again = std::min(std::max(g, cfg.againMin), again - cfg.againMinStep);
			if (g < cfg.againMin)
				exposure = std::min<int32_t>(static_cast<int32_t>(total / cfg.againMin), exposure - 1);
		} else {
			exposure = std::min<int32_t>(static_cast<int32_t>(total / again), exposure - 1);
		}
		exposure = std::clamp(exposure, cfg.exposureMin, cfg.exposureMax);
		again = std::clamp(again, cfg.againMin, cfg.againMax);
		LOG(IPASoftExposure, Debug) << "exposureMSV " << exposureMSV << " x" << f
					     << " exp " << exposure << " again " << again;
		return;
	}
''', "proportional AE")
  patch("src/ipa/simple/algorithms/agc.cpp", r'#include "agc\.h"\n', '\n#include <cmath>\n', "#include <cmath>")
