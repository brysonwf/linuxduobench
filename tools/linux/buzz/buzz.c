// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * buzz — play a force-feedback effect on an evdev device (the Duo's DA7280 haptics, docs/research/33).
 * usage: buzz [/dev/input/eventN] [ms=400] [level 0-32767=24000]
 * Uses FF_CONSTANT (what the DA7280 driver supports in its direct/constant mode).
 * build (WSL): aarch64-linux-gnu-gcc -static -O2 -I/root/hdr/include -o buzz buzz.c
 */
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/input/event0";
	int ms = argc > 2 ? atoi(argv[2]) : 400, level = argc > 3 ? atoi(argv[3]) : 24000;
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }
	char name[128] = "?"; ioctl(fd, EVIOCGNAME(sizeof(name)), name);
	struct ff_effect e; memset(&e, 0, sizeof(e));
	e.type = FF_CONSTANT; e.id = -1; e.replay.length = ms;
	e.u.constant.level = level;
	if (ioctl(fd, EVIOCSFF, &e) < 0) { perror("EVIOCSFF (upload effect)"); return 1; }
	struct input_event ev; memset(&ev, 0, sizeof(ev));
	ev.type = EV_FF; ev.code = e.id; ev.value = 1;
	if (write(fd, &ev, sizeof(ev)) != sizeof(ev)) { perror("play"); return 1; }
	printf("%s: playing FF_CONSTANT %d ms level %d (effect %d)\n", name, ms, level, e.id);
	usleep((ms + 100) * 1000);
	ioctl(fd, EVIOCRMFF, e.id);
	return 0;
}
