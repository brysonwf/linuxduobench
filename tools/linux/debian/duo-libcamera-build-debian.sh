#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Camera for Debian (2026-11-08): Debian trixie's libcamera 0.4 finds the IMX351 (simple pipeline + soft ISP) but has no
# IMX351 properties / sensor helper, no GPU soft ISP and no lens-shading correction, and its PipeWire has no libcamera
# plugin installed. This does on Debian what camera/duo-libcamera-build-pmos.sh does on postmarketOS:
#   1. libcamera MASTER (softisp LSC series merged 2026-10-22) + our IMX351 patch (camera/libcamera-imx351.py), with the
#      GPU soft ISP -> /usr/local (Debian's multiarch libdir)
#   2. PipeWire's libcamera SPA plugin, built from the SAME PipeWire version Debian runs (1.4.2 on trixie), against it
#      (API fixes as on pmOS + the frame-rate patch) -> /usr/lib/<triplet>/spa-0.2/libcamera/libspa-libcamera.so
#   3. the IMX351 tuning (Android's CCMs + LSC meshes, made on the phone) copied from the phone's own pmOS install, if any
# Builds fetched outside code on the phone (on disk, /var/tmp): the phone's owner runs it (or asks Claude to).
# usage (Git Bash, repo root, phone on Debian): sh tools/linux/debian/duo-libcamera-build-debian.sh
set -e
AL=artifacts/local; [ -d $AL ] || AL="$(git rev-parse --path-format=absolute --git-common-dir 2>/dev/null)/../artifacts/local"
H=root@169.254.66.1; K=${DUO_KEY:-$AL/duo}; C=$(dirname "$0")/../camera
O="-i $K -o ConnectTimeout=5 -o ServerAliveInterval=10 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
PW_V=$(ssh $O $H "dpkg-query -W -f '\${Version}' pipewire | sed 's/-.*//'"); echo "Debian pipewire $PW_V"
T=$AL/libcamera-master.tar.gz; P=$AL/pipewire-$PW_V.tar.gz
[ -s $T ] || curl -fL -o $T https://github.com/libcamera-org/libcamera/archive/refs/heads/master.tar.gz
[ -s $P ] || curl -fL -o $P https://gitlab.freedesktop.org/pipewire/pipewire/-/archive/$PW_V/pipewire-$PW_V.tar.gz
echo "sources: libcamera $(md5sum $T | cut -c1-8), pipewire $(md5sum $P | cut -c1-8)"
tr -d '\r' < $C/libcamera-imx351.py | ssh $O $H 'cat > /var/tmp/libcamera-imx351.py'
tr -d '\r' < $C/pipewire-libcamera-framerate.py | ssh $O $H 'cat > /var/tmp/pipewire-libcamera-framerate.py'
cat $T | ssh $O $H "cat > /var/tmp/libcamera-master.tar.gz"
cat $P | ssh $O $H "cat > /var/tmp/pipewire-$PW_V.tar.gz"
ssh $O $H "set -e; export DEBIAN_FRONTEND=noninteractive; cd /var/tmp
  # libgbm-dev pinned to the INSTALLED libgbm1: Debian runs Mesa 26 from trixie-backports (GPU), and plain trixie's
  # libgbm-dev 25.0.7 would force a downgrade ('Unable to satisfy dependencies', 2026-11-08)
  GBM=libgbm-dev=\$(dpkg-query -W -f '\${Version}' libgbm1)
  apt-get -q -y --no-install-recommends install build-essential meson ninja-build pkg-config python3-jinja2 python3-ply \
    python3-yaml libyaml-dev libudev-dev libgnutls28-dev libevent-dev libglib2.0-dev libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev libunwind-dev openssl libdrm-dev libegl-dev libgles-dev \$GBM libdbus-1-dev \
    libspa-0.2-dev libpipewire-0.3-dev > /var/tmp/cam-apt.log 2>&1 || { tail -5 /var/tmp/cam-apt.log; exit 1; }
  T3=\$(dpkg-architecture -qDEB_HOST_MULTIARCH)
  rm -rf lc lcb pw pwb; mkdir lc pw
  tar -xzf libcamera-master.tar.gz -C lc --strip-components=1; tar -xzf pipewire-$PW_V.tar.gz -C pw --strip-components=1
  python3 -I /var/tmp/libcamera-imx351.py /var/tmp/lc
  cd lc
  meson setup /var/tmp/lcb --prefix=/usr/local --buildtype=release -Db_ndebug=true -Dpipelines=simple -Dipas=softisp \
    -Dsoftisp-gpu=enabled -Dcam=enabled -Dqcam=disabled -Dgstreamer=enabled -Dv4l2=disabled -Dtracing=disabled \
    -Ddocumentation=disabled -Dpycamera=disabled -Dlc-compliance=disabled -Dtest=false -Dudev=enabled > /var/tmp/lc-setup.log 2>&1 ||
    { tail -25 /var/tmp/lc-setup.log; exit 1; }
  ninja -C /var/tmp/lcb > /var/tmp/lc-build.log 2>&1 || { tail -30 /var/tmp/lc-build.log; exit 1; }
  ninja -C /var/tmp/lcb install > /dev/null; ldconfig
  echo \"libcamera master installed in /usr/local (\$(ls /usr/local/lib/\$T3/libcamera.so.* | head -1))\"
  cd /var/tmp/pw
  sed -i 's/return std::move(model.value());/return std::string(model.value());/' spa/plugins/libcamera/libcamera-device.cpp
  sed -i 's/const std::vector<libcamera::FrameBuffer::Plane> &planes = bufs\[0\]->planes();/const auto planes = bufs[0]->planes();/' spa/plugins/libcamera/libcamera-utils.cpp
  grep -rq CameraLocationFront /usr/local/include/libcamera/libcamera/property_ids.h ||
    sed -i 's/properties::CameraLocation\(Front\|Back\|External\)/properties::Location\1/g' spa/plugins/libcamera/*.cpp
  python3 -I /var/tmp/pipewire-libcamera-framerate.py /var/tmp/pw
  PKG_CONFIG_PATH=/usr/local/lib/\$T3/pkgconfig meson setup /var/tmp/pwb --prefix=/usr --buildtype=release \
    --auto-features=disabled -Db_ndebug=true -Dcpp_std=c++20 -Dlibcamera=enabled -Dspa-plugins=enabled -Dsession-managers=[] \
    -Dexamples=disabled -Dtests=disabled -Dman=disabled -Ddocs=disabled > /var/tmp/pw-setup.log 2>&1 || { tail -25 /var/tmp/pw-setup.log; exit 1; }
  ninja -C /var/tmp/pwb spa/plugins/libcamera/libspa-libcamera.so > /var/tmp/pw-build.log 2>&1 || { tail -30 /var/tmp/pw-build.log; exit 1; }
  install -Dm644 /var/tmp/pwb/spa/plugins/libcamera/libspa-libcamera.so /usr/lib/\$T3/spa-0.2/libcamera/libspa-libcamera.so
  echo \"PipeWire $PW_V libcamera plugin installed\"
  # the IMX351 tuning from the phone's own pmOS install (Android's tables, made on the phone - never in a repo)
  mkdir -p /usr/local/share/libcamera/ipa/softisp
  if mkdir -p /mnt/pmos && mount -o ro /dev/disk/by-partlabel/spare /mnt/pmos 2>/dev/null; then
    [ -f /mnt/pmos/usr/share/libcamera/ipa/softisp/imx351.yaml ] && cp /mnt/pmos/usr/share/libcamera/ipa/softisp/imx351.yaml /usr/local/share/libcamera/ipa/softisp/ && echo 'tuning: imx351.yaml from pmOS'
    umount /mnt/pmos
  fi
  LD_LIBRARY_PATH=/usr/local/lib/\$T3 timeout 30 /usr/local/bin/cam -l 2>&1 | tail -3"
echo "done"
