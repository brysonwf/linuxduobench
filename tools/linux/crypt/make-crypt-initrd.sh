#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# OPTIONAL disk encryption (2026-11-07): build the small extra initrd (crypt.gz) that lets our initramfs unlock a
# LUKS2-encrypted installed system: cryptsetup + unl0kr (on-screen unlock keyboard) + udevd/udevadm (libinput needs
# them) + their libraries and data, taken from the INSTALLED postmarketOS root itself - the same file list pmOS's own
# initramfs uses (/usr/share/mkinitfs/files-extra/30-unl0kr.files). GRUB loads it after the initramfs only for an
# encrypted install; an unencrypted install never sees it.
# Runs ON the phone (busybox sh). usage: make-crypt-initrd.sh <installed-root, e.g. /mnt/ldb or /> <out crypt.gz>
set -e
R=${1%/}; OUT=$2
[ -n "$OUT" ] && [ -x "$R/sbin/cryptsetup" ] && [ -x "$R/usr/bin/unl0kr" ] || {
	echo "usage: $0 <root with cryptsetup + unl0kr (postmarketOS: apk add cryptsetup unl0kr)> <out.gz>"; exit 1; }
L=$R/usr/share/mkinitfs/files-extra/30-unl0kr.files
LIST="/sbin/cryptsetup /usr/bin/unl0kr /sbin/udevd /bin/udevadm /etc/unl0kr.conf /etc/unl0kr.conf.d /usr/share/X11/xkb
/usr/share/libinput /usr/lib/udev/libinput-device-group /usr/lib/udev/libinput-fuzz-extract /usr/lib/udev/libinput-fuzz-to-zero
/usr/lib/udev/rules.d"
# (all of rules.d, not only libinput's two: without 60-input-id/60-evdev libinput rejects every touchscreen, 2026-11-07)
[ -f $L ] && LIST="$LIST $(grep -v '^#' $L | sed 's/\*.*//')"
W=$(mktemp -d); chmod 755 $W
add() {   # $1 = absolute path inside the root (file, dir or symlink)
	p=$1; [ -e "$R$p" ] || [ -L "$R$p" ] || return 0
	[ -e "$W$p" ] || [ -L "$W$p" ] && return 0
	mkdir -p "$W$(dirname $p)"; cp -a "$R$p" "$W$p"
	[ -L "$R$p" ] && { t=$(readlink "$R$p"); case $t in /*) add "$t";; *) add "$(dirname $p)/$t";; esac; }
	return 0
}
libs() {   # shared libraries of one ELF file, resolved inside the root (musl: Alpine's lib dirs)
	f=$1; for n in $(readelf -d "$R$f" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'); do
		for d in /lib /usr/lib; do [ -e "$R$d/$n" ] && { add $d/$n; libs $d/$n; break; }; done
	done
}
command -v readelf > /dev/null || { echo "needs readelf (apk add binutils)"; exit 1; }
for p in $LIST; do add $p; done
for p in /sbin/cryptsetup /usr/bin/unl0kr /sbin/udevd /bin/udevadm; do
	q=$(readlink -f "$R$p" 2>/dev/null); q=${q#$R}; libs ${q:-$p}; done
add /lib/ld-musl-aarch64.so.1
(cd $W && find . -mindepth 1 | sed 's|^\./||' | LC_ALL=C sort | cpio -o -H newc -R 0:0 2>/dev/null) | gzip -9 > "$OUT"
echo "crypt initrd: $(find $W -type f | wc -l) files, $(du -sk $W | cut -f1) KB -> $OUT ($(( $(wc -c < "$OUT") / 1024 )) KB)"
rm -rf $W
