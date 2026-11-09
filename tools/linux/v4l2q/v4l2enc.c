// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * v4l2enc: minimal static V4L2 mem2mem ENCODER client (VENUS round 13, docs/research/33). Encodes N synthetic NV12
 * frames (moving gradient) to raw H.264 Annex-B with plain V4L2 ioctls, so the SAME client runs on the mainline
 * workbench (venus) and on Droidian (downstream msm_vidc, no ffmpeg there) - for comparing the two drivers' HFI
 * sequences. Prints every step; exits on the first error or after a timeout per buffer.
 * build (WSL): aarch64-linux-gnu-gcc -static -O2 -I/root/hdr/include -o v4l2enc v4l2enc.c
 * usage: [V4L2ENC_ION=1] [V4L2ENC_RC=vbr|cbr|cq] [V4L2ENC_BITRATE=bps] v4l2enc <dev> <width> <height> <frames> <out.h264>   (ION/USERPTR mode for Droidian)
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
struct buf { void *p[2]; size_t len[2]; unsigned np; int fd[2]; };

/* ION mode (Droidian / downstream msm_vidc, 4.14 kernel): only V4L2_MEMORY_USERPTR is accepted; the buffer is an ION
 * dma-buf whose fd goes in plane.reserved[0] (offset in reserved[1]). Set V4L2ENC_ION=1. msm ION ABI of 4.14. */
struct ion_allocation_data { unsigned long long len; unsigned heap_id_mask, flags, fd, unused; };
#define ION_IOC_ALLOC _IOWR('I', 0, struct ion_allocation_data)
#define ION_SYSTEM_HEAP_ID 25
static int ion_mode, ion_fd = -1;
static unsigned mem_type(void) { return ion_mode ? V4L2_MEMORY_USERPTR : V4L2_MEMORY_MMAP; }
static int ion_alloc(size_t len, void **cpu)
{
	struct ion_allocation_data a = { .len = len, .heap_id_mask = 1u << ION_SYSTEM_HEAP_ID };
	if (ioctl(ion_fd, ION_IOC_ALLOC, &a)) { perror("ION_IOC_ALLOC"); return -1; }
	*cpu = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, a.fd, 0);
	if (*cpu == MAP_FAILED) { perror("mmap ion"); return -1; }
	return a.fd;
}

static int xi(int fd, unsigned long r, void *a, const char *n)
{
	int ret = ioctl(fd, r, a);
	printf("%-16s %s\n", n, ret ? strerror(errno) : "ok");
	fflush(stdout);
	return ret;
}

static int setup(int fd, unsigned type, unsigned fourcc, unsigned w, unsigned h, struct buf *b, unsigned *nbuf)
{
	struct v4l2_format f = { .type = type };
	f.fmt.pix_mp.width = w; f.fmt.pix_mp.height = h; f.fmt.pix_mp.pixelformat = fourcc;
	f.fmt.pix_mp.num_planes = fourcc == V4L2_PIX_FMT_NV12 ? 1 : 1;
	if (xi(fd, VIDIOC_S_FMT, &f, type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ? "S_FMT out" : "S_FMT cap")) return -1;
	printf("  -> %ux%u planes %u size0 %u stride0 %u\n", f.fmt.pix_mp.width, f.fmt.pix_mp.height,
	       f.fmt.pix_mp.num_planes, f.fmt.pix_mp.plane_fmt[0].sizeimage, f.fmt.pix_mp.plane_fmt[0].bytesperline);
	struct v4l2_requestbuffers rb = { .count = NB, .type = type, .memory = mem_type() };
	if (xi(fd, VIDIOC_REQBUFS, &rb, "REQBUFS")) return -1;
	*nbuf = rb.count;
	if (rb.count > NB) rb.count = NB;
	if (ion_mode) {
		for (unsigned i = 0; i < rb.count; i++) {
			b[i].np = 1; b[i].len[0] = f.fmt.pix_mp.plane_fmt[0].sizeimage;
			b[i].fd[0] = ion_alloc(b[i].len[0], &b[i].p[0]);
			if (b[i].fd[0] < 0) return -1;
		}
		return f.fmt.pix_mp.plane_fmt[0].bytesperline;
	}
	for (unsigned i = 0; i < rb.count; i++) {
		struct v4l2_plane pl[2] = { 0 };
		struct v4l2_buffer vb = { .type = type, .memory = V4L2_MEMORY_MMAP, .index = i, .m.planes = pl, .length = 2 };
		if (ioctl(fd, VIDIOC_QUERYBUF, &vb)) { perror("QUERYBUF"); return -1; }
		b[i].np = vb.length;
		for (unsigned k = 0; k < vb.length; k++) {
			b[i].len[k] = pl[k].length;
			b[i].p[k] = mmap(0, pl[k].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, pl[k].m.mem_offset);
			if (b[i].p[k] == MAP_FAILED) { perror("mmap"); return -1; }
		}
	}
	return f.fmt.pix_mp.plane_fmt[0].bytesperline;
}

static int qbuf(int fd, unsigned type, unsigned i, struct buf *b, size_t used)
{
	struct v4l2_plane pl[2] = { 0 };
	for (unsigned k = 0; k < b[i].np; k++) {
		pl[k].length = b[i].len[k]; pl[k].bytesused = k ? 0 : used;
		if (ion_mode) { pl[k].m.userptr = (unsigned long)b[i].p[k]; pl[k].reserved[0] = b[i].fd[k]; pl[k].reserved[1] = 0; }
	}
	struct v4l2_buffer vb = { .type = type, .memory = mem_type(), .index = i, .m.planes = pl, .length = b[i].np };
	return ioctl(fd, VIDIOC_QBUF, &vb);
}

int main(int argc, char **argv)
{
	if (argc < 6) { fprintf(stderr, "usage: v4l2enc <dev> <w> <h> <frames> <out.h264>\n"); return 2; }
	unsigned w = atoi(argv[2]), h = atoi(argv[3]), n = atoi(argv[4]);
	ion_mode = getenv("V4L2ENC_ION") != NULL;
	if (ion_mode && (ion_fd = open("/dev/ion", O_RDWR)) < 0) { perror("/dev/ion"); return 1; }
	int fd = open(argv[1], O_RDWR | O_NONBLOCK);
	if (fd < 0) { perror(argv[1]); return 1; }
	FILE *out = fopen(argv[5], "wb");
	struct buf ob[NB], cb[NB]; unsigned no = 0, nc = 0;
	const unsigned OT = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, CT = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	/* V4L2ENC_RC=vbr|cbr|cq + V4L2ENC_BITRATE=<bps>: rate-control controls (VENUS round 13 RC bisect) */
	if (getenv("V4L2ENC_RC")) {
		const char *m = getenv("V4L2ENC_RC");
		struct v4l2_control c = { .id = V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
			.value = !strcmp(m, "cbr") ? V4L2_MPEG_VIDEO_BITRATE_MODE_CBR :
				 !strcmp(m, "cq") ? V4L2_MPEG_VIDEO_BITRATE_MODE_CQ : V4L2_MPEG_VIDEO_BITRATE_MODE_VBR };
		xi(fd, VIDIOC_S_CTRL, &c, "S_CTRL rc");
	}
	if (getenv("V4L2ENC_SKIP")) {	/* frame skip on -> mainline picks VBR_VFR / CBR_VFR */
		struct v4l2_control c = { .id = V4L2_CID_MPEG_VIDEO_FRAME_SKIP_MODE, .value = V4L2_MPEG_VIDEO_FRAME_SKIP_MODE_LEVEL_LIMIT };
		xi(fd, VIDIOC_S_CTRL, &c, "S_CTRL frameskip");
	}
	if (getenv("V4L2ENC_BITRATE")) {
		struct v4l2_control c = { .id = V4L2_CID_MPEG_VIDEO_BITRATE, .value = atoi(getenv("V4L2ENC_BITRATE")) };
		xi(fd, VIDIOC_S_CTRL, &c, "S_CTRL bitrate");
	}
	/* capture (bitstream) format first, as the stateful encoder spec says */
	if (setup(fd, CT, V4L2_PIX_FMT_H264, w, h, cb, &nc) < 0) return 1;
	int stride = setup(fd, OT, V4L2_PIX_FMT_NV12, w, h, ob, &no);
	if (stride < 0) return 1;
	if (!stride) stride = w;
	for (unsigned i = 0; i < nc; i++) if (qbuf(fd, CT, i, cb, 0)) { perror("QBUF cap"); return 1; }
	unsigned type = CT; if (xi(fd, VIDIOC_STREAMON, &type, "STREAMON cap")) return 1;
	type = OT; if (xi(fd, VIDIOC_STREAMON, &type, "STREAMON out")) return 1;
	unsigned sent = 0, got = 0, bytes = 0, free_out = no, idx_free[NB];
	for (unsigned i = 0; i < no; i++) idx_free[i] = i;
	while (got < n) {
		while (sent < n && free_out) {
			unsigned i = idx_free[--free_out];
			unsigned char *y = ob[i].p[0];
			unsigned lines = (ob[i].len[0] / stride) * 2 / 3;	/* aligned luma height */
			for (unsigned r = 0; r < h; r++) memset(y + r * stride, (r + sent * 4) & 0xff, w);
			memset(y + (size_t)stride * lines, 128, (size_t)stride * h / 2);
			if (qbuf(fd, OT, i, ob, ob[i].len[0])) { perror("QBUF out"); return 1; }
			sent++;
		}
		struct pollfd p = { .fd = fd, .events = POLLIN | POLLOUT | POLLPRI };
		int pr = poll(&p, 1, 5000);
		if (pr <= 0) { printf("TIMEOUT: sent %u frames, got %u packets (%u bytes)\n", sent, got, bytes); return 3; }
		struct v4l2_plane pl[2]; struct v4l2_buffer vb;
		memset(&vb, 0, sizeof(vb)); memset(pl, 0, sizeof(pl));
		vb.type = CT; vb.memory = mem_type(); vb.m.planes = pl; vb.length = ion_mode ? 1 : 2;
		while (!ioctl(fd, VIDIOC_DQBUF, &vb)) {
			fwrite(cb[vb.index].p[0], 1, pl[0].bytesused, out); bytes += pl[0].bytesused; got++;
			if (got <= 3 || got == n) printf("packet %u: %u bytes\n", got, pl[0].bytesused);
			if (qbuf(fd, CT, vb.index, cb, 0)) break;
			memset(&vb, 0, sizeof(vb)); memset(pl, 0, sizeof(pl));
			vb.type = CT; vb.memory = mem_type(); vb.m.planes = pl; vb.length = ion_mode ? 1 : 2;
		}
		memset(&vb, 0, sizeof(vb)); memset(pl, 0, sizeof(pl));
		vb.type = OT; vb.memory = mem_type(); vb.m.planes = pl; vb.length = ion_mode ? 1 : 2;
		while (!ioctl(fd, VIDIOC_DQBUF, &vb)) {
			idx_free[free_out++] = vb.index;
			memset(&vb, 0, sizeof(vb)); memset(pl, 0, sizeof(pl));
			vb.type = OT; vb.memory = mem_type(); vb.m.planes = pl; vb.length = ion_mode ? 1 : 2;
		}
	}
	printf("DONE: %u frames -> %u packets, %u bytes\n", n, got, bytes);
	fclose(out);
	return 0;
}
