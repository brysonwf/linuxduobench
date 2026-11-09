// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * duo-bootanim - the Linux Duo Bench boot animation on the Surface Duo's two panels (2026-11-07).
 *
 * Art: the approved "Linux Duo Bench boot art" (penguin with a headlamp on the TOP = camera screen = DSI-2, the lamp's
 * circle on the BOTTOM = keyboard screen = DSI-1, laptop posture), 900x675 P6 PPM frames shown at exactly 2x, rotated
 * like drmfill's DRMFILL_ROT=1 (upright with the Duo held like a laptop). Credits: tools/linux/bootanim/CREDITS.md.
 *
 * usage: duo-bootanim <frame-dir> [hinge-angle-deg=90] [max-seconds=30]
 *   <frame-dir>/seq.txt: "fps N", then "L <top> <bottom>" per loop frame and one "E <top> <bottom>" end frame; names
 *   are PPM basenames in <frame-dir>, "{a}" = the rendered hinge angle nearest to the argument (3 digits).
 *   Loops until /run/duo-boot-done exists (init writes it) or max-seconds pass, then shows the end frame (lamp on,
 *   centred) for 1.5 s and exits. Each panel is double-buffered (page flips with events, no tearing on the command-
 *   mode panels). Static build like drmfill (kernel uapi headers only).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#define MAXF 128
#define IW 900
#define IH 675

static int xioctl(int fd, unsigned long req, void *arg)
{
	int r;
	do { r = ioctl(fd, req, arg); } while (r == -1 && (errno == EINTR || errno == EAGAIN));
	return r;
}

/* ---- frames */
struct frame { char name[64]; uint8_t *rgb; };
static struct frame frames[MAXF]; static int nframes;
static const char *dir;

static int ppm_tok(FILE *f)
{
	int c, v = 0;
	while ((c = fgetc(f)) != EOF) { if (c == '#') { while ((c = fgetc(f)) != EOF && c != '\n'); continue; } if (c >= '0' && c <= '9') break; }
	if (c == EOF) return -1;
	do v = v * 10 + (c - '0'); while ((c = fgetc(f)) >= '0' && c <= '9');
	return v;
}

static int frame_id(const char *name)		/* load on first use, return its index */
{
	for (int i = 0; i < nframes; i++) if (!strcmp(frames[i].name, name)) return i;
	if (nframes == MAXF) return -1;
	char p[512]; snprintf(p, sizeof(p), "%s/%s.ppm", dir, name);
	FILE *f = fopen(p, "rb"); char m[2];
	if (!f || fread(m, 1, 2, f) != 2 || m[0] != 'P' || m[1] != '6') { fprintf(stderr, "bootanim: bad frame %s\n", p); if (f) fclose(f); return -1; }
	int w = ppm_tok(f), h = ppm_tok(f), mx = ppm_tok(f);	/* ppm_tok already ate the one whitespace after maxval */
	uint8_t *rgb = malloc(IW * IH * 3);
	if (w != IW || h != IH || mx != 255 || !rgb || fread(rgb, 3, IW * IH, f) != IW * IH) { fprintf(stderr, "bootanim: %s is not %dx%d\n", p, IW, IH); fclose(f); free(rgb); return -1; }
	fclose(f);
	snprintf(frames[nframes].name, sizeof(frames[nframes].name), "%s", name); frames[nframes].rgb = rgb;
	return nframes++;
}

/* ---- sequence */
struct step { int top, bottom; };
static struct step seq[512], endstep; static int nseq, fps = 15;

static void subst(char *out, size_t n, const char *in, int angle)
{
	const char *a = strstr(in, "{a}");
	if (!a) { snprintf(out, n, "%s", in); return; }
	snprintf(out, n, "%.*s%03d%s", (int)(a - in), in, angle, a + 3);
}

static int load_seq(int angle)
{
	char p[512], kind[4], t[64], b[64], tt[64], bb[64]; snprintf(p, sizeof(p), "%s/seq.txt", dir);
	FILE *f = fopen(p, "r"); if (!f) { perror(p); return -1; }
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "fps %d", &fps) == 1) continue;
		if (sscanf(line, "%3s %63s %63s", kind, t, b) != 3) continue;
		subst(tt, sizeof(tt), t, angle); subst(bb, sizeof(bb), b, angle);
		struct step s = { frame_id(tt), frame_id(bb) };
		if (s.top < 0 || s.bottom < 0) { fclose(f); return -1; }
		if (kind[0] == 'E') endstep = s; else if (nseq < 512) seq[nseq++] = s;
	}
	fclose(f);
	return nseq ? 0 : -1;
}

/* ---- panels */
struct buf { uint32_t fb, pitch; uint8_t *p; };
struct panel { uint32_t conn, crtc, w, h; struct drm_mode_modeinfo mode; struct buf b[2]; int cur, pending; };
static struct panel top, bottom; static int fd;

/* 2x scale + quarter turn clockwise (drmfill rot 1: landscape art upright in laptop posture) */
static void draw(struct panel *pn, struct buf *bf, const uint8_t *rgb)
{
	const uint32_t lw = pn->h, lh = pn->w;			/* landscape canvas */
	const uint32_t ox = (lw - IW * 2) / 2, oy = (lh - IH * 2) / 2;
	for (uint32_t y = 0; y < pn->h; y++) {
		uint32_t *row = (uint32_t *)(bf->p + (size_t)y * bf->pitch);
		for (uint32_t x = 0; x < pn->w; x++) {
			uint32_t lx = y, ly = pn->w - 1 - x;
			if (lx < ox || ly < oy || lx >= ox + IW * 2 || ly >= oy + IH * 2) { row[x] = 0; continue; }
			const uint8_t *s = rgb + (((ly - oy) >> 1) * IW + ((lx - ox) >> 1)) * 3;
			row[x] = (uint32_t)s[0] << 16 | s[1] << 8 | s[2];
		}
	}
}

static int make_buf(struct panel *pn, struct buf *bf)
{
	struct drm_mode_create_dumb cd = { .width = pn->w, .height = pn->h, .bpp = 32 };
	if (xioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) return -1;
	struct drm_mode_fb_cmd2 fb = { .width = cd.width, .height = cd.height, .pixel_format = 0x34325258 /* XR24 */ };
	fb.handles[0] = cd.handle; fb.pitches[0] = cd.pitch;
	if (xioctl(fd, DRM_IOCTL_MODE_ADDFB2, &fb)) return -1;
	struct drm_mode_map_dumb md = { .handle = cd.handle };
	if (xioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md)) return -1;
	bf->p = mmap(0, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
	if (bf->p == MAP_FAILED) return -1;
	memset(bf->p, 0, cd.size); bf->fb = fb.fb_id; bf->pitch = cd.pitch;
	return 0;
}

static int find_panels(void)
{
	uint32_t conns[16], crtcs[16], encs[16];
	struct drm_mode_card_res res = { 0 };
	if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) return -1;
	if (res.count_connectors > 16 || res.count_crtcs > 16 || res.count_encoders > 16) return -1;
	res.connector_id_ptr = (uintptr_t)conns; res.crtc_id_ptr = (uintptr_t)crtcs; res.encoder_id_ptr = (uintptr_t)encs; res.count_fbs = 0;
	if (xioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) return -1;
	for (uint32_t i = 0; i < res.count_connectors; i++) {
		struct drm_mode_modeinfo modes[8];
		struct drm_mode_get_connector c = { .connector_id = conns[i] };
		if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) || c.connector_type != DRM_MODE_CONNECTOR_DSI ||
		    c.connection != 1 || !c.count_modes || c.count_modes > 8) continue;
		uint32_t nm = c.count_modes, tid = c.connector_type_id;
		memset(&c, 0, sizeof(c)); c.connector_id = conns[i]; c.count_modes = nm; c.modes_ptr = (uintptr_t)modes;
		if (xioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c)) continue;
		struct panel *pn = tid == 2 ? &top : tid == 1 ? &bottom : NULL;	/* DSI-2 = camera screen = top */
		if (!pn) continue;
		struct drm_mode_get_encoder e = { .encoder_id = c.encoder_id };
		pn->crtc = (c.encoder_id && !xioctl(fd, DRM_IOCTL_MODE_GETENCODER, &e) && e.crtc_id) ? e.crtc_id : crtcs[i < res.count_crtcs ? i : 0];
		pn->conn = conns[i]; pn->mode = modes[0]; pn->w = modes[0].hdisplay; pn->h = modes[0].vdisplay;
		if (make_buf(pn, &pn->b[0]) || make_buf(pn, &pn->b[1])) return -1;
		struct drm_mode_crtc sc = { .crtc_id = pn->crtc, .fb_id = pn->b[0].fb, .mode_valid = 1, .mode = pn->mode,
					    .count_connectors = 1, .set_connectors_ptr = (uintptr_t)&pn->conn };
		if (xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &sc)) { perror("SETCRTC"); return -1; }
	}
	return top.conn && bottom.conn ? 0 : -1;
}

static void wait_flips(void)
{
	char ev[1024];
	while (top.pending || bottom.pending) {
		struct pollfd p = { .fd = fd, .events = POLLIN };
		if (poll(&p, 1, 200) <= 0) { top.pending = bottom.pending = 0; return; }	/* a lost event never blocks us */
		ssize_t n = read(fd, ev, sizeof(ev));
		for (ssize_t o = 0; o + (ssize_t)sizeof(struct drm_event) <= n; ) {
			struct drm_event *e = (struct drm_event *)(ev + o);
			if (e->type == DRM_EVENT_FLIP_COMPLETE) {
				struct drm_event_vblank *v = (struct drm_event_vblank *)e;
				if (v->user_data == 1) top.pending = 0; else bottom.pending = 0;
			}
			o += e->length;
		}
	}
}

static void show(struct panel *pn, int f, uint64_t tag)
{
	struct buf *bf = &pn->b[pn->cur ^ 1];
	draw(pn, bf, frames[f].rgb);
	struct drm_mode_crtc_page_flip pf = { .crtc_id = pn->crtc, .fb_id = bf->fb, .flags = DRM_MODE_PAGE_FLIP_EVENT, .user_data = tag };
	if (!xioctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf)) { pn->pending = 1; pn->cur ^= 1; }
	else {	/* no flip support: modeset to the new buffer */
		struct drm_mode_crtc sc = { .crtc_id = pn->crtc, .fb_id = bf->fb, .mode_valid = 1, .mode = pn->mode,
					    .count_connectors = 1, .set_connectors_ptr = (uintptr_t)&pn->conn };
		if (!xioctl(fd, DRM_IOCTL_MODE_SETCRTC, &sc)) pn->cur ^= 1;
	}
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <frame-dir> [hinge-deg=90] [max-s=30]\n", argv[0]); return 2; }
	dir = argv[1];
	int deg = argc > 2 ? atoi(argv[2]) : 90, maxs = argc > 3 ? atoi(argv[3]) : 30;
	static const int rendered[] = { 30, 60, 90, 120, 150, 180 };
	int angle = 90;
	for (unsigned i = 0, best = 999; i < sizeof(rendered) / sizeof(*rendered); i++)
		if ((unsigned)abs(rendered[i] - deg) < best) { best = abs(rendered[i] - deg); angle = rendered[i]; }
	double t0 = now();
	if (load_seq(angle)) { fprintf(stderr, "bootanim: cannot load %s/seq.txt\n", dir); return 1; }
	fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
	if (fd < 0) fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror("card"); return 1; }
	xioctl(fd, DRM_IOCTL_SET_MASTER, 0);
	if (find_panels()) { fprintf(stderr, "bootanim: need both DSI panels\n"); return 1; }
	printf("bootanim: %d frames, %d steps at %d fps, hinge %d -> %d, loaded in %.2f s\n", nframes, nseq, fps, deg, angle, now() - t0);
	fflush(stdout);
	struct stat st; double period = 1.0 / fps, next = now(), tmax = now() + maxs; long shown = 0, late = 0;
	for (int i = 0; stat("/run/duo-boot-done", &st) && now() < tmax; i = (i + 1) % nseq) {
		show(&top, seq[i].top, 1); show(&bottom, seq[i].bottom, 2); wait_flips(); shown++;
		next += period; double d = next - now();
		if (d > 0) { struct timespec ts = { (time_t)d, (long)((d - (time_t)d) * 1e9) }; nanosleep(&ts, 0); }
		else { late++; next = now(); }
	}
	show(&top, endstep.top, 1); show(&bottom, endstep.bottom, 2); wait_flips();
	printf("bootanim: %ld frames shown, %ld late, end frame\n", shown, late); fflush(stdout);
	usleep(1500000);
	return 0;	/* closing the fd drops our framebuffers; the next DRM user (fbcon/desktop/phosh) takes over */
}
