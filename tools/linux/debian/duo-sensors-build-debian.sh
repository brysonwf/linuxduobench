#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Sensors for Debian trixie (2026-11-08): trixie packages neither libssc nor an iio-sensor-proxy built with it, so the
# Duo's SLPI sensors (accelerometer = auto-rotate, light = auto-brightness, proximity, compass) never reach the desktop.
# Debian sid HAS them: libssc 0.4.4 and iio-sensor-proxy 3.9 built with -Dssc-support=enabled on arm64. This BACKPORTS
# both source packages (Debian's own packaging, apt-get source from sid's deb-src, no sid binaries) and builds real .debs
# on the phone, installs them, adds the Duo's udev rule (the four sensors + the accelerometer mount matrix on the
# FastRPC devices, as on pmOS), and copies the .debs to artifacts/local/debian-debs (duo-debian-prepare.sh installs
# them from there). Verified 2026-11-08: monitor-sensor shows orientation, tilt, light (lux), proximity, compass live;
# libssc 0.4.4 does not need our 0.2.2 open_sync fix (the proxy stays responsive with all four sensors open).
# Run from the PC (Git Bash, repo root), phone on Debian with SSH; it builds fetched source: the phone's owner runs it
# (or asks Claude to). usage: sh tools/linux/debian/duo-sensors-build-debian.sh [phone-ip]
set -e
AL=artifacts/local; [ -d $AL ] || AL="$(git rev-parse --path-format=absolute --git-common-dir 2>/dev/null)/../artifacts/local"
H=root@${1:-169.254.66.1}; K=${DUO_KEY:-$AL/duo}
O="-i $K -o ConnectTimeout=5 -o ServerAliveInterval=10 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
ssh $O $H "set -e; export DEBIAN_FRONTEND=noninteractive
  echo 'deb-src http://deb.debian.org/debian sid main' > /etc/apt/sources.list.d/sid-src.list
  apt-get -q update > /dev/null; apt-get -q -y --no-install-recommends install dpkg-dev devscripts equivs > /dev/null
  rm -rf /var/tmp/bp; mkdir -p /var/tmp/bp; cd /var/tmp/bp; apt-get -q source libssc iio-sensor-proxy > /dev/null
  for p in libssc-* iio-sensor-proxy-*; do
    [ -d \$p ] || continue
    (cd \$p && mk-build-deps -i -r -t 'apt-get -y --no-install-recommends' debian/control > ../bd-\$p.log 2>&1) || { tail -15 bd-\$p.log; exit 1; }
    (cd \$p && dpkg-buildpackage -b -us -uc -j8 > ../build-\$p.log 2>&1) || { tail -30 build-\$p.log; exit 1; }
    case \$p in libssc-*) apt-get -y install ./libssc2_*.deb ./libssc-dev_*.deb ./gir1.2-ssc*.deb > /dev/null ;; esac
  done
  apt-get -y install ./iio-sensor-proxy_*.deb > /dev/null
  printf '# Surface Duo: SLPI sensors via hexagonrpcd + libssc for iio-sensor-proxy (duo-sensors service)\nSUBSYSTEM==\"misc\", KERNEL==\"fastrpc-*\", ENV{IIO_SENSOR_PROXY_TYPE}=\"ssc-accel ssc-light ssc-proximity ssc-compass\", ENV{ACCEL_MOUNT_MATRIX}=\"-1, 0, 0; 0, -1, 0; 0, 0, 1\"\n' > /etc/udev/rules.d/81-duo-ssc.rules
  udevadm control --reload; udevadm trigger --subsystem-match=misc --action=change; systemctl restart iio-sensor-proxy
  sleep 4; systemctl is-active iio-sensor-proxy"
mkdir -p $AL/debian-debs
for f in $(ssh $O $H "ls /var/tmp/bp/*.deb | grep -v -e dbgsym -e build-deps"); do
  ssh $O $H "cat $f" > $AL/debian-debs/$(basename $f)
done
ls -la $AL/debian-debs
echo "done - check: monitor-sensor (on the phone)"
