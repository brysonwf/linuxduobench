// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * v4l2cap: minimal static V4L2 capture test (camera step 8, docs/research/33 "CAMERA"). Fills every MMAP buffer with a
 * marker byte before QBUF, captures N frames and reports per buffer how many bytes still hold the marker / are zero /
 * other, then writes the last frame to a file. Tells "the VFE wrote nothing" (marker survives) from "it wrote zeros".
 * build (WSL): aarch64-linux-gnu-gcc -static -O2 -I/root/hdr/include -o v4l2cap v4l2cap.c
 * usage: v4l2cap <dev> <frames> <out.raw> [marker=0xaa]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define NB 4
int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: v4l2cap <dev> <frames> <out.raw> [marker]\n"); return 2; }
	int n = atoi(argv[2]); unsigned mk = argc > 4 ? strtoul(argv[4], 0, 0) : 0xaa;
	int fd = open(argv[1], O_RDWR);
	if (fd < 0) { perror(argv[1]); return 1; }
	const unsigned T = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	struct v4l2_format f = { .type = T };
	if (ioctl(fd, VIDIOC_G_FMT, &f)) { perror("G_FMT"); return 1; }
	printf("fmt %ux%u sizeimage %u\n", f.fmt.pix_mp.width, f.fmt.pix_mp.height, f.fmt.pix_mp.plane_fmt[0].sizeimage);
	struct v4l2_requestbuffers rb = { .count = NB, .type = T, .memory = V4L2_MEMORY_MMAP };
	if (ioctl(fd, VIDIOC_REQBUFS, &rb)) { perror("REQBUFS"); return 1; }
	unsigned char *p[NB]; size_t len[NB];
	for (unsigned i = 0; i < rb.count; i++) {
		struct v4l2_plane pl[1] = { 0 };
		struct v4l2_buffer b = { .type = T, .memory = V4L2_MEMORY_MMAP, .index = i, .m.planes = pl, .length = 1 };
		if (ioctl(fd, VIDIOC_QUERYBUF, &b)) { perror("QUERYBUF"); return 1; }
		len[i] = pl[0].length;
		p[i] = mmap(0, len[i], PROT_READ | PROT_WRITE, MAP_SHARED, fd, pl[0].m.mem_offset);
		if (p[i] == MAP_FAILED) { perror("mmap"); return 1; }
		memset(p[i], mk, len[i]);
		b.m.planes = pl; pl[0].bytesused = 0;
		if (ioctl(fd, VIDIOC_QBUF, &b)) { perror("QBUF"); return 1; }
	}
	unsigned t = T;
	if (ioctl(fd, VIDIOC_STREAMON, &t)) { perror("STREAMON"); return 1; }
	int last = -1;
	for (int k = 0; k < n; k++) {
		struct pollfd pf = { .fd = fd, .events = POLLIN };
		if (poll(&pf, 1, 3000) <= 0) { printf("timeout at frame %d\n", k); break; }
		struct v4l2_plane pl[1] = { 0 };
		struct v4l2_buffer b = { .type = T, .memory = V4L2_MEMORY_MMAP, .m.planes = pl, .length = 1 };
		if (ioctl(fd, VIDIOC_DQBUF, &b)) { perror("DQBUF"); break; }
		size_t m = 0, z = 0, o = 0;
		for (size_t j = 0; j < len[b.index]; j += 64) {
			unsigned char c = p[b.index][j];
			if (c == mk) m++; else if (!c) z++; else o++;
		}
		printf("frame %d buf %u seq %u flags %#x bytesused %u: marker %zu zero %zu other %zu (sampled /64)\n", k, b.index,
		       b.sequence, b.flags, pl[0].bytesused, m, z, o);
		last = b.index;
		if (k < n - 1) { memset(p[b.index], mk, len[b.index]); pl[0].bytesused = 0; ioctl(fd, VIDIOC_QBUF, &b); }
	}
	ioctl(fd, VIDIOC_STREAMOFF, &t);
	if (last >= 0) { FILE *o = fopen(argv[3], "wb"); fwrite(p[last], 1, len[last], o); fclose(o); }
	return 0;
}
