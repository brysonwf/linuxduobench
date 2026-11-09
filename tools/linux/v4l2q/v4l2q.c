// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * v4l2q: tiny static V4L2 query tool for the workbench (no v4l-utils in the initramfs). Prints the device caps and the
 * pixel formats offered on the OUTPUT (bitstream in, for a decoder) and CAPTURE queues (multi-planar), plus the frame
 * sizes of each compressed format. Used to prove the Venus firmware answers (VENUS round 9, docs/research/33).
 * build (WSL): aarch64-linux-gnu-gcc -static -O2 -I/root/hdr/include -o v4l2q v4l2q.c
 * usage: v4l2q [/dev/video0]
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

static void fourcc(unsigned v, char *o) { o[0] = v; o[1] = v >> 8; o[2] = v >> 16; o[3] = v >> 24; o[4] = 0; }

static void list(int fd, unsigned type, const char *name)
{
	for (unsigned i = 0;; i++) {
		struct v4l2_fmtdesc d = { .index = i, .type = type };
		if (ioctl(fd, VIDIOC_ENUM_FMT, &d)) { if (!i) printf("%s: none\n", name); break; }
		char f[5]; fourcc(d.pixelformat, f);
		printf("%s[%u] %s '%s'%s", name, i, f, d.description, d.flags & V4L2_FMT_FLAG_COMPRESSED ? " compressed" : "");
		struct v4l2_frmsizeenum s = { .index = 0, .pixel_format = d.pixelformat };
		if (!ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &s)) {
			if (s.type == V4L2_FRMSIZE_TYPE_DISCRETE) printf(" %ux%u", s.discrete.width, s.discrete.height);
			else printf(" %ux%u..%ux%u", s.stepwise.min_width, s.stepwise.min_height,
				    s.stepwise.max_width, s.stepwise.max_height);
		}
		printf("\n");
	}
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/video0";
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }
	struct v4l2_capability c = { 0 };
	if (ioctl(fd, VIDIOC_QUERYCAP, &c)) { perror("QUERYCAP"); return 1; }
	printf("%s: driver %s card '%s' bus %s caps %#x\n", dev, c.driver, c.card, c.bus_info, c.device_caps);
	list(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, "OUTPUT");
	list(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, "CAPTURE");
	close(fd);
	return 0;
}
