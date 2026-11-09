#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Install the Surface Duo system layer (docs/research/workbench/generic-layout.md) into a root filesystem - any
# distribution, systemd or OpenRC. Run it from a checkout (the files next to this script), on the phone or in a chroot
# host:
#   sh tools/linux/system/install.sh --init=systemd|openrc [--root=DIR] [--desktop=plasma|phosh|none] [--enable]
# Installs: libexec/* + the charger guard + the speaker tuning tool -> /usr/libexec/duo; systemd units ->
# /etc/systemd/system or OpenRC wrappers -> /etc/init.d; the desktop hook (phosh: phoc.ini layout) -> /usr/libexec/duo.
# --enable also enables the services (systemctl --root / rc-update in a chroot). Packages, firmware and desktop
# settings stay with the distribution's setup script (tools/linux/debian/duo-debian-prepare.sh, pmOS: duo-pmos-prepare.sh).
set -e
D=$(cd "$(dirname "$0")" && pwd); ROOT=; INIT=; DESK=none; EN=0
for a in "$@"; do case $a in
	--init=*) INIT=${a#*=} ;; --root=*) ROOT=${a#*=} ;; --desktop=*) DESK=${a#*=} ;; --enable) EN=1 ;;
	*) echo "usage: $0 --init=systemd|openrc [--root=DIR] [--desktop=plasma|phosh|none] [--enable]"; exit 1 ;;
esac; done
case $INIT in systemd|openrc) ;; *) echo "--init=systemd or --init=openrc"; exit 1 ;; esac
ROOT=${ROOT%/}
# services, in start order (gps-assist needs the duo-gps tool: enabled only when it is installed)
SVC="duo-watchdog duo-bootanim duo-hw duo-bootanim-done duo-charger duo-modem duo-sensors duo-bt-addr duo-mtp duo-autobrightness"

# write a new file and rename it over the old one: never truncate a script that is running (sh reads scripts as it
# goes - rewriting the live watchdog feeder in place could kill it, and a dead feeder = a watchdog reset)
put() { mkdir -p "$(dirname "$ROOT$2")"; tr -d '\r' < "$1" > "$ROOT$2.new"; chmod "$3" "$ROOT$2.new"; mv -f "$ROOT$2.new" "$ROOT$2"; }

for f in "$D"/libexec/*; do put "$f" /usr/libexec/duo/${f##*/} 755; done
[ -f "$D/../duo-charger-guard.sh" ] && put "$D/../duo-charger-guard.sh" /usr/libexec/duo/duo-charger-guard 755
if [ -f "$D/../audio-tuning/duo-speaker-tuning.sh" ]; then   # + its ACDB readers (found next to it)
	put "$D/../audio-tuning/duo-speaker-tuning.sh" /usr/libexec/duo/duo-speaker-tuning 755
	for p in acdb-dump acdb-cal acdb-topologies; do put "$D/../audio-tuning/$p.py" /usr/libexec/duo/$p.py 644; done
fi
# the Linux Duo Bench icon (the boot animation's headlamp penguin, 128 px; tools/linux/bootanim/CREDITS.md): menus and
# launchers use it by name, e.g. Hingeboard's start-menu button (request 2026-11-08)
if [ -f "$D/../bootanim/linuxduobench-icon.png" ]; then
	mkdir -p "$ROOT/usr/share/icons/hicolor/128x128/apps"
	cp "$D/../bootanim/linuxduobench-icon.png" "$ROOT/usr/share/icons/hicolor/128x128/apps/linuxduobench.png"
	chmod 644 "$ROOT/usr/share/icons/hicolor/128x128/apps/linuxduobench.png"
fi
case $DESK in
	phosh) put "$D/../desktop/phosh/duo-hw-desktop" /usr/libexec/duo/duo-hw-desktop 755 ;;
	plasma|none) rm -f "$ROOT/usr/libexec/duo/duo-hw-desktop" ;;
	*) echo "unknown --desktop=$DESK"; exit 1 ;;
esac
if [ $INIT = systemd ]; then
	for f in "$D"/systemd/*.service "$D"/systemd/*.timer; do [ -f "$f" ] || continue; put "$f" /etc/systemd/system/${f##*/} 644; done
	# drop-ins for other packages' units (e.g. ModemManager.service.d: low-power before it starts)
	for f in "$D"/systemd/*.d/*.conf; do [ -f "$f" ] || continue; d=${f%/*}; put "$f" /etc/systemd/system/${d##*/}/${f##*/} 644; done
else
	for f in "$D"/openrc/*; do put "$f" /etc/init.d/${f##*/} 755; done
fi
echo "installed the Duo system layer ($INIT, desktop $DESK) into ${ROOT:-/}: $(ls "$ROOT/usr/libexec/duo" | wc -l) files in /usr/libexec/duo"

[ $EN = 1 ] || exit 0
# systemctl for ROOT: the host's with --root, or - on the phone-side installer, which has no systemctl of its own -
# the target's, offline in a chroot (2026-11-08)
sctl() {
	if [ -z "$ROOT" ]; then systemctl "$@"
	elif command -v systemctl > /dev/null; then systemctl --root="$ROOT" "$@"
	else chroot "$ROOT" systemctl "$@"; fi
}
# self-recovery (systemd): duo-boot-check.timer marks the slot good 2 min after a healthy boot; qbootctl's own unit
# (marks it at once) is masked
if [ $INIT = systemd ] && [ -x "$ROOT/usr/bin/qbootctl" ] && [ -f "$D/systemd/duo-boot-check.timer" ]; then
	SVC="$SVC duo-boot-check.timer"
	sctl mask qbootctl.service > /dev/null 2>&1
fi
G=; [ -x "$ROOT/usr/libexec/duo/duo-gps-assist" ] && { [ -x "$ROOT/usr/local/bin/duo-gps" ] || { [ -z "$ROOT" ] && command -v duo-gps > /dev/null; }; } && G=duo-gps-assist
if [ $INIT = systemd ]; then
	[ -n "$ROOT" ] || systemctl daemon-reload
	sctl enable $SVC $G > /dev/null 2>&1
else
	for s in $SVC $G; do
		case $s in duo-watchdog|duo-bootanim) lvl=boot ;; *) lvl=default ;; esac
		if [ -n "$ROOT" ]; then chroot "$ROOT" rc-update add $s $lvl > /dev/null; else rc-update add $s $lvl > /dev/null; fi
	done
fi
echo "enabled: $SVC $G"
