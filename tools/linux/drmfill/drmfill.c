// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drmfill — put a test pattern on every connected DRM connector using a dumb buffer (no fbdev, no libdrm).
 * Workbench tool for the Duo's display bring-up (docs/research/34-display.md): checks whether frames carry
 * our pixels when /dev/fb0 (fbdev emulation) is suspect.
 * usage: drmfill [/dev/dri/card1] [black|white|split|checker|bars|uefi|uefi-mul|uefi-a0|ppm:<file>] [seconds-to-hold=30]
 *        (ppm: DRMFILL_ROT=0..3, default 1 = upright in laptop posture)
 *   env DRMFILL_CYCLE=1: switch each CRTC off first (panel unprepare/reset), then on with the pattern (one by one)
 *   env DRMFILL_CYCLE=2: all CRTCs off, then all on back-to-back
 * build (WSL): make -C <linux> ARCH=arm64 headers_install INSTALL_HDR_PATH=/root/hdr, then
 *   aarch64-linux-gnu-gcc -static -O2 -I/root/hdr/include -o drmfill drmfill.c
 *   (kernel uapi headers: drm/drm.h + drm/drm_mode.h)
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#define MAXN 16

static int xioctl(int fd, unsigned long req, void *arg)
{
	int r;
	do { r = ioctl(fd, req, arg); } while (r == -1 && (errno == EINTR || errno == EAGAIN));
	return r;
}

/*
 * 'ppm:<file>' (2026-11-04, the boot splash - tools/linux/splash): show a P3/P6 PPM centred on each panel, scaled up
 * by the largest whole factor that fits, rotated by DRMFILL_ROT (0..3 quarter turns clockwise, like fbcon=rotate:N;
 * 1 = upright with the Duo held like a laptop). The kernel's own boot logo can't be used: it is garbled on simpledrm
 * and msm's fbdev refuses imageblit ("framebuffer is not in virtual address space").
 */
static uint8_t *img; static uint32_t iw, ih;
static int ppm_token(FILE *f)
{
	int c, v = 0;
	while ((c = fgetc(f)) != EOF) {
		if (c == '#') { while ((c = fgetc(f)) != EOF && c != '\n'); continue; }
		if (c >= '0' && c <= '9') break;
	}
	if (c == EOF) return -1;
	do v = v * 10 + (c - '0'); while ((c = fgetc(f)) >= '0' && c <= '9');
	return v;
}
static int ppm_load(const char *path)
{
	FILE *f = fopen(path, "rb"); char m[3] = { 0 };
	if (!f || fread(m, 1, 2, f) != 2 || m[0] != 'P' || (m[1] != '3' && m[1] != '6')) { perror(path); return -1; }
	iw = ppm_token(f); ih = ppm_token(f); int mx = ppm_token(f);
	if ((int)iw <= 0 || (int)ih <= 0 || mx <= 0 || !(img = malloc((size_t)iw * ih * 3))) return -1;
	if (m[1] == '6') { fgetc(f); if (fread(img, 3, (size_t)iw * ih, f) != (size_t)iw * ih) return -1; }
	else for (size_t i = 0; i < (size_t)iw * ih * 3; i++) img[i] = ppm_token(f) * 255 / mx;
	fclose(f); return 0;
}
static uint32_t ppm_pix(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int rot)
{
	uint32_t lx, ly, lw = (rot & 1) ? h : w, lh = (rot & 1) ? w : h;	/* landscape canvas for odd rotations */
	switch (rot & 3) {
	case 1: lx = y; ly = w - 1 - x; break;
	case 2: lx = w - 1 - x; ly = h - 1 - y; break;
	case 3: lx = h - 1 - y; ly = x; break;
	default: lx = x; ly = y;
	}
	uint32_t s = lw / iw < lh / ih ? lw / iw : lh / ih; if (!s) s = 1;
	uint32_t ox = (lw - iw * s) / 2, oy = (lh - ih * s) / 2;
	if (lx < ox || ly < oy || lx >= ox + iw * s || ly >= oy + ih * s) return 0;
	const uint8_t *p = img + (((size_t)(ly - oy) / s) * iw + (lx - ox) / s) * 3;
	return (uint32_t)p[0] << 16 | p[1] << 8 | p[2];
}

static uint32_t pix(const char *pat, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	static int rot = -1;
	if (img) { if (rot < 0) rot = getenv("DRMFILL_ROT") ? atoi(getenv("DRMFILL_ROT")) : 1; return ppm_pix(x, y, w, h, rot); }
	static const uint32_t bars[6] = { 0xff0000, 0x00ff00, 0x0000ff, 0xffffff, 0x000000, 0x808080 };
	if (!strcmp(pat, "black")) return 0;
	if (!strcmp(pat, "white")) return 0xffffff;
	if (!strcmp(pat, "split")) return y < h / 2 ? 0 : 0xffffff;
	if (!strcmp(pat, "checker")) return ((x / 150) + (y / 150)) % 2 ? 0xffffff : 0;
	return bars[x * 6 / w > 5 ? 5 : x * 6 / w];
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/dri/card1";
	const char *pat = argc > 2 ? argv[2] : "bars";
	int hold = argc > 3 ? atoi(argv[3]) : 30;
	int fd = open(dev, O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror(dev); return 1; }
	if (xioctl(fd, DRM_IOCTL_SET_MASTER, 0)) perror("SET_MASTER (continuing)");
	if (!strncmp(pat, "ppm:", 4) && ppm_load(pat + 4)) { fprintf(stderr, "cannot load %s\n", pat + 4); return 1; }

	/*
	 * 'uefi': show the UEFI's own framebuffer pixels (physical 0x80600000, 1350x1800, stride 5400, a8r8g8b8 — the DT
	 * simple-framebuffer) through the msm pipeline, so the DSI MISR of the same frame can be compared with the UEFI's
	 * (docs/research/34 "DSI MISR"). Boot without simpledrm/fbcon so nothing has drawn over it.
	 */
	const uint8_t *uefi = NULL;
	int blend = !strcmp(pat, "uefi-mul") ? 1 : !strcmp(pat, "uefi-a0") ? 2 : 0;	/* emulate the UEFI's alpha blend */
	if (!strncmp(pat, "uefi", 4)) {
		int mf = open("/dev/mem", O_RDONLY | O_SYNC);
		if (mf < 0) { perror("/dev/mem"); return 1; }
		uefi = mmap(0, 5400 * 1800, PROT_READ, MAP_SHARED, mf, 0x80600000);
		if (uefi == MAP_FAILED) { perror("mmap uefi fb"); return 1; }
	}

	uint32_t conns[MAXN], crtcs[MAXN], encs[MAXN];
	struct drm_mode_card_res res = { 0 };
	if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) { perror("GETRESOURCES"); return 1; }
	if (res.count_connectors > MAXN) res.count_connectors = MAXN;
	if (res.count_crtcs > MAXN) res.count_crtcs = MAXN;
	if (res.count_encoders > MAXN) res.count_encoders = MAXN;
	res.connector_id_ptr = (uintptr_t)conns; res.crtc_id_ptr = (uintptr_t)crtcs;
	res.encoder_id_ptr = (uintptr_t)encs; res.count_fbs = 0;
	if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) { perror("GETRESOURCES 2"); return 1; }

	if (getenv("DRMFILL_CYCLE") && !strcmp(getenv("DRMFILL_CYCLE"), "2")) {
		/* DRMFILL_CYCLE=2: all CRTCs off, then all back on back-to-back (like fbcon's boot-time enable of both) */
		for (uint32_t c = 0; c < res.count_crtcs; c++) {
			struct drm_mode_crtc off = { .crtc_id = crtcs[c] };
			printf("crtc %u off: %d\n", crtcs[c], xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &off) ? -errno : 0);
		}
		sleep(1);
	}
	for (uint32_t i = 0; i < res.count_connectors; i++) {
		/* The kernel copies the mode list only if the buffer holds ALL modes; a short buffer (the old fixed 8) got
		 * nothing, so a TV with many modes was driven with the previous connector's stack leftovers = the phone
		 * panel's 1350x1800 timing -> the slanted/blue DP picture of 2026-11-04. Size the buffer from the count. */
		struct drm_mode_modeinfo modes[128];
		struct drm_mode_get_connector c = { .connector_id = conns[i] };
		if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c)) continue;
		if (c.connection != 1 || !c.count_modes) { printf("connector %u: not connected\n", conns[i]); continue; }
		uint32_t nm = c.count_modes;
		if (nm > 128) { printf("connector %u: %u modes, too many\n", conns[i], nm); continue; }
		memset(&c, 0, sizeof(c)); memset(modes, 0, sizeof(modes));
		c.connector_id = conns[i]; c.count_modes = nm; c.modes_ptr = (uintptr_t)modes;
		c.count_props = 0; c.count_encoders = 0;
		if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c)) { perror("GETCONNECTOR 2"); continue; }
		if (!modes[0].hdisplay) { printf("connector %u: mode list changed, skipped\n", conns[i]); continue; }
		/* DRMFILL_MODE=WxH[@Hz]: use the first matching mode instead of the preferred one (connectors without it
		 * keep their preferred mode), e.g. DRMFILL_MODE=1920x1080@60 for 60 Hz HDMI */
		const char *want = getenv("DRMFILL_MODE");
		if (want) {
			unsigned ww = 0, wh = 0, wr = 0;
			if (sscanf(want, "%ux%u@%u", &ww, &wh, &wr) >= 2)
				for (uint32_t k = 0; k < nm; k++)
					if (modes[k].hdisplay == ww && modes[k].vdisplay == wh && (!wr || modes[k].vrefresh == wr)) {
						modes[0] = modes[k]; break;
					}
		}
		printf("connector %u: mode %s %ux%u@%u clock %u\n", conns[i], modes[0].name, modes[0].hdisplay,
		       modes[0].vdisplay, modes[0].vrefresh, modes[0].clock);
		struct drm_mode_get_encoder e = { .encoder_id = c.encoder_id };
		uint32_t crtc = 0;
		if (c.encoder_id && !xioctl(fd, DRM_IOCTL_MODE_GETENCODER, &e) && e.crtc_id) crtc = e.crtc_id;
		if (!crtc) crtc = crtcs[i < res.count_crtcs ? i : 0];
		struct drm_mode_modeinfo *m = &modes[0];

		struct drm_mode_create_dumb cd = { .width = m->hdisplay, .height = m->vdisplay, .bpp = 32 };
		if (xioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) { perror("CREATE_DUMB"); continue; }
		struct drm_mode_fb_cmd2 fb = { .width = cd.width, .height = cd.height,
					       .pixel_format = 0x34325258 /* XR24 */ };
		fb.handles[0] = cd.handle; fb.pitches[0] = cd.pitch;
		if (xioctl(fd, DRM_IOCTL_MODE_ADDFB2, &fb)) { perror("ADDFB2"); continue; }
		struct drm_mode_map_dumb md = { .handle = cd.handle };
		if (xioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md)) { perror("MAP_DUMB"); continue; }
		uint8_t *p = mmap(0, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
		if (p == MAP_FAILED) { perror("mmap"); continue; }
		for (uint32_t y = 0; y < cd.height; y++) {
			uint32_t *row = (uint32_t *)(p + (size_t)y * cd.pitch);
			if (uefi) {
				const uint32_t *src = (const uint32_t *)(uefi + (size_t)(y < 1800 ? y : 1799) * 5400);
				uint32_t n = cd.width < 1350 ? cd.width : 1350;
				for (uint32_t x = 0; x < n; x++) {
					uint32_t v = src[x], a = v >> 24;
					if (blend == 1)		/* coverage: rgb * alpha */
						v = ((((v >> 16) & 0xff) * a / 255) << 16) | ((((v >> 8) & 0xff) * a / 255) << 8) |
						    ((v & 0xff) * a / 255);
					else if (blend == 2 && a == 0)	/* alpha-0 pixels -> black */
						v = 0;
					row[x] = v;
				}
				continue;
			}
			for (uint32_t x = 0; x < cd.width; x++)
				row[x] = pix(pat, x, y, cd.width, cd.height);
		}
		if (getenv("DRMFILL_CYCLE") && strcmp(getenv("DRMFILL_CYCLE"), "2")) {	/* power the pipe off first: panel unprepare + reset, then prepare + init */
			struct drm_mode_crtc off = { .crtc_id = crtc };
			printf("crtc %u off: %d\n", crtc, xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &off) ? -errno : 0);
			sleep(1);
		}
		struct drm_mode_crtc sc = { .crtc_id = crtc, .fb_id = fb.fb_id, .mode_valid = 1, .mode = *m,
					    .count_connectors = 1, .set_connectors_ptr = (uintptr_t)&conns[i] };
		int r = xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &sc);
		struct drm_mode_fb_dirty_cmd dirty = { .fb_id = fb.fb_id };
		xioctl(fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);
		printf("connector %u -> crtc %u: %ux%u pitch %u '%s' setcrtc=%d\n", conns[i], crtc,
		       cd.width, cd.height, cd.pitch, pat, r ? -errno : 0);
	}
	fflush(stdout);
	sleep(hold);		/* keep the fbs alive (closing the fd drops them) */
	return 0;
}
