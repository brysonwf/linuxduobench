# SPDX-License-Identifier: GPL-2.0-or-later
# Shared helpers for the Surface Duo system scripts in /usr/libexec/duo (sourced, not run). Distribution- and init-neutral:
# no OpenRC/systemd helpers inside the scripts - this file covers what they used (docs/research/workbench/generic-layout.md).

# log: stdout (the init system's log) + the kernel log (survives into the next boot's saved console)
log() { echo "${DUO_NAME:-duo}: $*"; echo "${DUO_NAME:-duo}: $*" > /dev/kmsg 2>/dev/null; }

# a partition's block device name by its GPT name (sdX), e.g. part modem_a
part() { for d in /sys/class/block/sd*; do grep -q "^PARTNAME=$1\$" $d/uevent 2>/dev/null && echo ${d##*/}; done | head -1; }

# the kernel modules we ship (per kernel release, else the flat older set)
duo_modules() { if [ -d /lib/duo/$(uname -r) ]; then echo /lib/duo/$(uname -r); else echo /lib/duo; fi; }
duo_insmod() { m=$(duo_modules)/$1.ko; shift
	grep -q "^$(basename "$m" .ko | tr - _) " /proc/modules || insmod "$m" "$@" 2>/dev/null; }

# a background daemon with a pidfile: daemon_start <pidfile> <logfile> <command...>; daemon_stop <pidfile>
daemon_start() { _p=$1; _o=$2; shift 2; nohup "$@" >> "$_o" 2>&1 < /dev/null & echo $! > "$_p"; }
daemon_stop() { [ -s "$1" ] && kill "$(cat "$1")" 2>/dev/null; rm -f "$1"; }
daemon_alive() { [ -s "$1" ] && kill -0 "$(cat "$1")" 2>/dev/null; }

# another system service (systemd or OpenRC; the distributions name some differently): service <name> start|stop|status
service() { _n=$1; _a=$2
	if [ -d /run/systemd/system ]; then
		case $_n in modemmanager) _n=ModemManager ;; networkmanager) _n=NetworkManager ;; esac
		if [ "$_a" = status ]; then systemctl is-active --quiet "$_n"; else systemctl "$_a" "$_n"; fi
	else rc-service "$_n" "$_a"; fi; }

# the phone's vendor_a (inside super, one extent at sector 11369472) mounted read-only at /mnt/vendor-fw
#   On a phone without Android (Debian only: super deleted) the files our drivers need from vendor_a live in a copy
#   made by duo-vendor-copy before the installer removed super: /var/lib/duo/vendor-fw (never shipped - made on the
#   phone from its own vendor_a). vendor_mount_at <dir> mounts whichever exists, read-only.
VENDOR_COPY=/var/lib/duo/vendor-fw
vendor_mount_at() { _d=$1; mkdir -p $_d; mountpoint -q $_d && return 0
	S=$(part super)
	# vendor_a's place inside super comes from super's own metadata (duo-lp-extents.py; this phone: sector 11369472,
	# one extent) - other Duos can have another layout. Falls back to that offset where python3 is missing.
	if [ -n "$S" ]; then
		_x=$(python3 -I /usr/libexec/duo/duo-lp-extents.py /dev/$S vendor_a 2>/dev/null)
		[ $(echo "$_x" | grep -c .) -le 1 ] || { echo "vendor_a has several extents in super: not supported" >&2; return 1; }
		_o=${_x%% *}; _o=${_o:-11369472}
	fi
	if [ -n "$S" ]; then L=$(losetup -f) && losetup -r -o $((_o * 512)) $L /dev/$S && mount -t ext4 -o ro $L $_d
	elif [ -d $VENDOR_COPY/firmware ]; then mount --bind $VENDOR_COPY $_d && mount -o remount,bind,ro $_d
	else return 1; fi; }
vendor_mount() { vendor_mount_at /mnt/vendor-fw; }
