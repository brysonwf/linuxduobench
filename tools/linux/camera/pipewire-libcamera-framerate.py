#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""PipeWire 1.4.7 libcamera SPA plugin: advertise a FRAME-RATE range in EnumFormat (Surface Duo, 2026-11-07).

Why: the plugin lists only sizes. GNOME Snapshot's camera library (Aperture) runs its resolution choice once per frame
rate it finds in the caps (best_resolution_for_fps), so with no framerate it never chose: GStreamer fixated the caps on
its own (1280x720 on the Duo) and the preview could not follow Aperture's (or our) aspect preference. The range comes
from the camera's FrameDurationLimits control (the soft IPA publishes it, camera/libcamera-imx351.py patch 3): max fps =
1e6 / min frame duration, min fps = 1e6 / max frame duration, default 30 (or the max if lower). Newer PipeWire does
the same upstream.
usage: pipewire-libcamera-framerate.py <pipewire-source-dir>   (idempotent)
"""
import re, sys

f = sys.argv[1] + "/spa/plugins/libcamera/libcamera-utils.cpp"
s = open(f).read()
MARK = "Surface Duo patch: frame rate"
if MARK in s:
    print("framerate patch: already applied"); sys.exit(0)
# re-apply over the first version (range) if it is there
s = re.sub(r"\t\{\n\t\t/\* Surface Duo patch: frame-rate range.*?\n\t\}\n\n", "", s, flags=re.S)
anchor = "\tfmt = (struct spa_pod*) spa_pod_builder_pop(&b, &f[0]);\n\n\tif (spa_pod_filter(&b, &result.param, fmt, filter) < 0)\n\t\tgoto next;\n"
if s.count(anchor) != 1:
    sys.exit("framerate patch: anchor not found exactly once in enum_format - PipeWire version changed?")
add = '''	{
		/* %s (FrameDurationLimits, us) - Aperture/Snapshot picks its resolution per frame rate */
		const ControlInfoMap &cinfo = impl->camera->controls();
		auto it = cinfo.find(&libcamera::controls::FrameDurationLimits);
		if (it != cinfo.end()) {
			int64_t minfd = it->second.min().get<int64_t>(), maxfd = it->second.max().get<int64_t>();
			if (minfd > 0 && maxfd >= minfd) {
				uint32_t maxr = 1000000 / minfd, minr = 1000000 / maxfd;
				if (minr < 1)
					minr = 1;
				if (maxr < minr)
					maxr = minr;
				uint32_t defr = maxr < 30 ? maxr : 30;
				/* one FIXED rate, not a range: Aperture reads a fixed fraction per caps structure (a range gave
				 * no selection either, 2026-11-07) */
				spa_pod_builder_prop(&b, SPA_FORMAT_VIDEO_framerate, 0);
				spa_pod_builder_fraction(&b, defr, 1);
			}
		}
	}

''' % MARK
s = s.replace(anchor, add + anchor)
open(f, "w").write(s)
print("framerate patch: applied")
