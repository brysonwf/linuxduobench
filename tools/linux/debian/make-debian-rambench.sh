#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# (WSL root) The Debian RAM bench (2026-11-08): the phone's RAM system built
# from DEBIAN trixie arm64 packages instead of Alpine, so the whole release is one distro and debootstrap installs Debian
# with Debian's own tools. No debootstrap run, no maintainer scripts: the .debs are fetched from deb.debian.org (their
# Release/Packages signatures are not checked here - the Packages index comes over https), unpacked into a work tree,
# and only the programs we name + the libraries they link (DT_NEEDED walk) are kept - like make-boot-initramfs.sh.
#   MODE=boot  (default) what a phone's boot image needs: /init + busybox, the panel/touch modules, LUKS unlock
#              (cryptsetup + unl0kr + udev), and the installer (dropbear, duo-install, gdisk, e2fsprogs, debootstrap,
#              gpgv, Debian's keyring). Must fit ABL's boot image (19 MB boots).
#   MODE=bench the dev/rescue shell on top: Wi-Fi, Bluetooth, sound, debug tools, every module of the kernel build.
# usage: [MODE=boot|bench] make-debian-rambench.sh <repo> <full-initramfs.cpio.gz (for lib/duo modules)> <out.cpio.gz>
set -e
REPO=$1; MODS=$2; OUT=$3; MODE=${MODE:-boot}; L=$REPO/tools/linux
[ -d "$L/initramfs" ] && [ -f "$MODS" ] && [ -n "$OUT" ] || { sed -n '2,13p' "$0"; exit 1; }
OUT=$(readlink -f "$OUT"); MIRROR=http://deb.debian.org/debian; C=/root/deb-cache; mkdir -p $C
W=$(mktemp -d); F=$W/full; S=$W/slim; mkdir -p $F $S
[ -f $C/Packages ] && [ -n "$(find $C/Packages -mmin -1440)" ] || curl -sfL https://deb.debian.org/debian/dists/trixie/main/binary-arm64/Packages.xz | xz -d > $C/Packages

PK="busybox-static kmod util-linux cryptsetup-bin unl0kr udev dropbear-bin gdisk e2fsprogs zstd debootstrap gpgv
    debian-archive-keyring grep perl-base dash coreutils dpkg"
BENCH="iproute2 iw wpasupplicant bluez alsa-utils evtest strace less nano htop tmux i2c-tools usbutils file
       dbus-daemon libqmi-utils rmtfs tqftpserv qrtr-tools"
[ $MODE = bench ] && PK="$PK $BENCH"
# packages resolved with their dependencies (first alternative); never: perl, debconf, systemd as init
python3 -I - $C/Packages $PK > $W/debs <<'EOF'
import sys
idx, want = sys.argv[1], sys.argv[2:]
# (busybox: the DYNAMIC one shares /usr/bin/busybox with busybox-static and overwrote it - /init could not start,
#  "no working init" panic, 2026-11-08)
skip = {'busybox', 'debconf', 'debconf-2.0', 'systemd', 'systemd-sysv', 'init-system-helpers', 'adduser', 'passwd',
        'base-files', 'mawk', 'sensible-utils', 'libpam-runtime', 'login', 'lsb-base', 'sysvinit-utils'}
pk, prov, cur = {}, {}, {}
for line in open(idx, encoding='utf-8', errors='replace').read().split('\n') + ['']:
    if not line:
        if 'Package' in cur and cur['Package'] not in pk:
            pk[cur['Package']] = cur
            for p in cur.get('Provides', '').split(','):
                p = p.strip().split(' ')[0]
                if p: prov.setdefault(p, cur['Package'])
        cur = {}; continue
    if line[0] in ' \t': continue
    k, _, v = line.partition(':'); cur[k] = v.strip()
seen = []
def add(n):
    n = n.split(':')[0]
    if n not in pk: n = prov.get(n, n)
    if n in skip or n in seen or n not in pk: return
    seen.append(n)
    for f in ('Pre-Depends', 'Depends'):
        for d in pk[n].get(f, '').split(','):
            if d.strip(): add(d.split('|')[0].strip().split(' ')[0])
for w in want: add(w)
for n in seen: print(pk[n]['Filename'])
EOF
echo "$(wc -l < $W/debs) packages"
while read fn; do
	b=$C/$(basename $fn); [ -s $b ] || curl -sfo $b $MIRROR/$fn || { echo "download failed: $fn"; exit 1; }
	dpkg-deb -x $b $F
done < $W/debs
cd $F
keep() { for p in "$@"; do [ -e "$p" ] || [ -L "$p" ] || continue; mkdir -p "$S/$(dirname "$p")"; cp -a "$p" "$S/$p"; done; }
# usr-merged layout like Debian: /bin /sbin /lib -> usr/...
mkdir -p $S/usr/bin $S/usr/sbin $S/usr/lib; ln -s usr/bin $S/bin; ln -s usr/sbin $S/sbin; ln -s usr/lib $S/lib
keep usr/bin/busybox
readelf -l $S/usr/bin/busybox | grep -q 'Requesting program interpreter' && { echo "usr/bin/busybox is not the static one - stop"; exit 1; }
# applet links: the arm64 busybox cannot run on the PC (`--list`), so take the names of the busybox links in the given
# (Alpine) initramfs - exactly the set /init runs with today (2026-11-08: an empty list left /init without cat/mount/ip)
mkdir -p $W/a; (cd $W/a && zcat "$MODS" | cpio -idm --quiet 'bin/*' 'sbin/*' 'usr/bin/*' 'usr/sbin/*' 2>/dev/null)
AP=$(cd $W/a && find . -type l | while read l; do case "$(readlink $l)" in *busybox) basename $l;; esac; done | sort -u
     echo setsid udhcpd)   # (Alpine has these two in busybox-extras; Debian's busybox-static has them)
[ $(echo "$AP" | wc -w) -gt 100 ] || { echo "only $(echo "$AP" | wc -w) busybox applet names found in $MODS"; exit 1; }
for a in $AP; do [ -e $S/usr/bin/$a ] || [ -e $S/usr/sbin/$a ] || ln -s busybox $S/usr/bin/$a; done
echo "busybox applets: $(echo "$AP" | wc -w)"
[ -L $S/usr/bin/sh ] || ln -s busybox $S/usr/bin/sh
# programs (real binaries win over busybox applets of the same name)
P="usr/sbin/cryptsetup usr/bin/unl0kr usr/bin/udevadm usr/lib/systemd/systemd-udevd usr/bin/kmod usr/sbin/blkid
   usr/sbin/switch_root usr/bin/mountpoint usr/sbin/dropbear usr/sbin/sgdisk usr/sbin/mke2fs usr/sbin/e2fsck usr/bin/zstd
   usr/bin/gpgv usr/bin/grep usr/bin/dash usr/sbin/chroot usr/bin/dpkg-deb"
# (dpkg-deb: debootstrap's preferred extractor; without it under dash it picked `ar` = a busybox link, and failed)
# (dash + coreutils' chroot: Debian's busybox-static PREFERS ITS APPLETS - `chroot X dpkg` ran busybox's dpkg inside the
#  new root and `sh -c grep` ran busybox grep (phone test 2026-11-08). duo-install runs the Debian install under dash,
#  which finds the real chroot, dpkg and grep through PATH; /init itself stays on busybox.)
[ $MODE = bench ] && P="$P usr/sbin/ip usr/sbin/iw usr/sbin/wpa_supplicant usr/bin/bluetoothctl usr/bin/btmgmt
   usr/bin/aplay usr/bin/amixer usr/bin/speaker-test usr/bin/evtest usr/bin/strace usr/bin/less usr/bin/nano usr/bin/htop
   usr/bin/tmux usr/sbin/i2cget usr/sbin/i2cset usr/sbin/i2cdetect usr/bin/lsusb usr/bin/file usr/bin/dbus-daemon
   usr/bin/qmicli usr/bin/rmtfs usr/bin/tqftpserv"
for p in $P; do [ -e $p ] || { echo "WARNING: $p not in the packages"; continue; }; rm -f $S/$p; keep $p; done
ln -sf kmod $S/usr/bin/insmod; ln -sf kmod $S/usr/bin/modprobe; ln -sf kmod $S/usr/bin/lsmod; ln -sf mke2fs $S/usr/sbin/mkfs.ext4
ln -sf ../lib/systemd/systemd-udevd $S/usr/sbin/udevd
# Debian's archive keyring: since 2025.1 the .gpg names are SYMLINKS to .pgp files - keep both (a lone .gpg link
# left debootstrap without a keyring = no Release signature check, 2026-11-08)
keep usr/lib/udev/rules.d etc/udev usr/share/unl0kr usr/share/keyrings/debian-archive-keyring.pgp usr/share/keyrings/debian-archive-keyring.gpg
keep usr/sbin/debootstrap usr/share/debootstrap
# debootstrap 1.0.141 needs perl (its pkgdetails; "No pkgdetails available" without it, phone test 2026-11-08):
# only the interpreter from Debian's perl-base: pkgdetails is inline perl with no modules (the 600 core module files
# would add 3 MB to the boot image)
keep usr/bin/perl; todo_extra=usr/bin/perl
[ $MODE = bench ] && keep usr/share/alsa usr/lib/firmware/regulatory.db usr/share/terminfo/l usr/share/terminfo/x usr/share/terminfo/v
# libraries: DT_NEEDED, recursively, from the work tree's multiarch dirs; then the loader
need() { readelf -d "$1" 2>/dev/null | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'; }
todo="$P $todo_extra"; done_=""
while [ -n "$todo" ]; do
	set -- $todo; f=$1; shift; todo="$*"; [ -f "$f" ] || continue
	for lib in $(need $f); do
		case " $done_ " in *" $lib "*) continue;; esac; done_="$done_ $lib"
		p=$(ls usr/lib/aarch64-linux-gnu/$lib lib/aarch64-linux-gnu/$lib usr/lib/aarch64-linux-gnu/*/$lib 2>/dev/null | head -1)
		[ -n "$p" ] || { echo "WARNING: $lib not found for $f"; continue; }
		keep $p; [ -L "$p" ] && keep "$(dirname $p)/$(readlink $p)"; todo="$todo $p"
	done
done
keep usr/lib/ld-linux-aarch64.so.1 lib/ld-linux-aarch64.so.1 usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1
[ -e $S/usr/lib/ld-linux-aarch64.so.1 ] || ln -s aarch64-linux-gnu/ld-linux-aarch64.so.1 $S/usr/lib/ld-linux-aarch64.so.1
# libinput data + udev's input helper; xkb: the base rules + the US layout only (unl0kr's keyboard), not every
# language (3.9 MB); udev's hwdb.d sources are left out (9.6 MB, uncompiled - no use here)
X=usr/share/X11/xkb
keep usr/share/libinput usr/lib/udev/input_id $X/rules/evdev $X/rules/evdev.lst $X/keycodes $X/types $X/compat
for s in us pc inet level3 latin srvr_ctrl keypad altwin capslock kpdl compose; do keep $X/symbols/$s; done
# ours: /init, duo-install, devmem (static, cross-built), the kernel's modules from the given initramfs
cd $W; tr -d '\r' < $L/initramfs/init > $S/init; chmod 755 $S/init
# (both optional: the source-only public release ships neither - nothing in it writes the phone's boot partitions)
[ -f $L/installer/duo-install.sh ] && { tr -d '\r' < $L/installer/duo-install.sh > $S/usr/sbin/duo-install; chmod 755 $S/usr/sbin/duo-install; }
# bootloader menu -> Recovery mode = back to stock Android (sourced by /init on an ABL boot without force_normal_boot)
[ -f $L/abl-stage1/recovery.sh ] && { tr -d '\r' < $L/abl-stage1/recovery.sh > $S/recovery.sh; chmod 755 $S/recovery.sh; }
aarch64-linux-gnu-gcc -static -O2 -o $S/usr/bin/devmem $L/devmem/devmem.c
mkdir -p $W/m; (cd $W/m && zcat "$MODS" | cpio -idm --quiet 'lib/duo/*' 2>/dev/null)
if [ $MODE = boot ]; then mkdir -p $S/usr/lib/duo; cp $W/m/lib/duo/panel-microsoft-surface-duo.ko $W/m/lib/duo/spi-hid-duo.ko $S/usr/lib/duo/
else cp -a $W/m/lib/duo $S/usr/lib/; fi
printf 'root:x:0:0:root:/root:/bin/sh\n' > $S/etc/passwd 2>/dev/null || { mkdir -p $S/etc; printf 'root:x:0:0:root:/root:/bin/sh\n' > $S/etc/passwd; }
printf 'root:x:0:\n' > $S/etc/group; mkdir -p $S/root/.ssh $S/dev $S/proc $S/sys $S/tmp $S/run $S/newroot $S/mnt $S/var/log
chmod 700 $S/root/.ssh; echo duo > $S/etc/hostname
# KEYS=<authorized_keys> = a dev key baked in (releases leave it out: the Windows installer appends the user's key)
[ -n "${KEYS:-}" ] && { tr -d '\r' < "$KEYS" > $S/root/.ssh/authorized_keys; chmod 600 $S/root/.ssh/authorized_keys; }
(cd $S && find . | sort | cpio -o -H newc -R 0:0 --quiet | gzip -9 > "$OUT")
echo "Debian RAM bench ($MODE): $(du -h "$OUT" | cut -f1) ($(find $S -type f | wc -l) files, $(du -sh $S | cut -f1) unpacked)"
for c in sh busybox insmod blkid switch_root mountpoint cryptsetup unl0kr udevd udevadm dropbear duo-install sgdisk mkfs.ext4 e2fsck debootstrap gpgv grep devmem; do
	p=$(cd $S && ls -d usr/bin/$c usr/sbin/$c 2>/dev/null | head -1); printf '%s:%s ' $c "${p:+ok}"; done; echo
rm -rf $W
