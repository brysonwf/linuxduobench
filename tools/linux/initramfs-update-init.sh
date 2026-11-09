#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Replace just /init (and rebuild our small static tools: drmfill) inside an existing workbench initramfs (no
# package/firmware rebuild needed).
# usage (WSL root): initramfs-update-init.sh <repo> <in.cpio.gz> <out.cpio.gz>
set -e
REPO=$1; IN=$2; OUT=$3; W=$(mktemp -d); chmod 755 $W	# the archive's "." = the initramfs root (755, not mktemp's 700)
cd "$W"; zcat "$IN" | cpio -idm --quiet
tr -d '\r' < "$REPO/tools/linux/initramfs/init" > init; chmod 755 init
# charger (docs/research/33 "Charger"): guard script + the qcom_smbx module built for the CURRENT kernel tree
mkdir -p usr/local/bin lib/duo
tr -d '\r' < "$REPO/tools/linux/duo-charger-guard.sh" > usr/local/bin/duo-charger-guard.sh; chmod 755 usr/local/bin/duo-charger-guard.sh
# speaker DSP tuning generator (run by /init; adsp-calibration.md) - tools only, the tuning is made on the phone
mkdir -p lib/duo/audio-tuning
for f in acdb-dump.py acdb-cal.py acdb-topologies.py duo-speaker-tuning.sh; do
  tr -d '\r' < "$REPO/tools/linux/audio-tuning/$f" > lib/duo/audio-tuning/$f; chmod 755 lib/duo/audio-tuning/$f
done
# the kernel BUILD dir (2026-11-07: the clean kernel builds out of tree - tools/linux/kernel/build.sh - so the .ko files
# and .config live in /root/build-duo; the old in-tree /root/linux still works via KTREE=/root/linux)
K=${KTREE:-/root/build-duo}
if [ -f $K/drivers/power/supply/qcom_smbx.ko ]; then cp $K/drivers/power/supply/qcom_smbx.ko lib/duo/
  echo "qcom_smbx.ko: $(modinfo -F vermagic lib/duo/qcom_smbx.ko) / params: $(modinfo -F parm lib/duo/qcom_smbx.ko | cut -c1-40)"
else echo "WARNING: no qcom_smbx.ko in $K (charger stays off)"; fi
# sound card machine driver (module since kernel #60; init loads it)
for S in $K/sound/soc/codecs/snd-soc-rt5663.ko $K/sound/soc/qcom/snd-soc-sdm845.ko; do if [ -f $S ]; then cp $S lib/duo/; echo "${S##*/}: depends=$(modinfo -F depends $S)"; else echo "WARNING: no ${S##*/}"; fi; done
# haptics (2026-11-04): DA7280 driver module + our static buzz tool (/usr/local/bin/buzz)
[ -f $K/drivers/input/misc/da7280.ko ] && cp $K/drivers/input/misc/da7280.ko lib/duo/ && echo "da7280.ko: copied" || echo "WARNING: no da7280.ko"
# Venus video codec + its media stack (init loads them in this order when the DT has video-codec@aa00000)
mkdir -p lib/duo/venus
for m in drivers/media/mc/mc drivers/media/v4l2-core/videodev drivers/media/common/videobuf2/videobuf2-common \
         drivers/media/common/videobuf2/videobuf2-v4l2 drivers/media/v4l2-core/v4l2-mem2mem \
         drivers/media/common/videobuf2/videobuf2-memops drivers/media/common/videobuf2/videobuf2-dma-contig \
         drivers/media/platform/qcom/venus/venus-core drivers/media/platform/qcom/venus/venus-dec \
         drivers/media/platform/qcom/venus/venus-enc; do
  [ -f $K/$m.ko ] && cp $K/$m.ko lib/duo/venus/ || echo "WARNING: no ${m##*/}.ko"
done
echo "venus modules: $(ls lib/duo/venus | wc -l)"
# Camera (2026-11-05, docs/research/workbench/camera.md): CAMSS + CCI + camcc + the V4L2 helpers from the tree, plus our
# out-of-tree imx351.ko (drivers/imx351/build-oot.sh). init loads them when the DT has camss@acb3000.
mkdir -p lib/duo/camera
for m in drivers/media/v4l2-core/v4l2-async drivers/media/v4l2-core/v4l2-fwnode drivers/media/v4l2-core/v4l2-cci \
         drivers/media/common/videobuf2/videobuf2-dma-sg drivers/clk/qcom/camcc-sm8150 drivers/i2c/busses/i2c-qcom-cci \
         drivers/media/platform/qcom/camss/qcom-camss; do
  [ -f $K/$m.ko ] && cp $K/$m.ko lib/duo/camera/ || echo "WARNING: no ${m##*/}.ko"
done
I=${IMX351_KO:-/root/imx351-build/imx351.ko}
[ -f $I ] && cp $I lib/duo/camera/ && echo "imx351.ko: $(modinfo -F vermagic $I)" || echo "WARNING: no imx351.ko ($I) - camera stays off"
echo "camera modules: $(ls lib/duo/camera | wc -l)"
# 2026-11-06: autofocus (Dongwoon DW9800L via mainline dw9719, built out of tree: CONFIG_VIDEO_DW9719 is off), the
# flash LED (led-class-flash + leds-qcom-flash) and the power key (qcom-pon populates pon@800's pwrkey/resin)
D=${DW9719_KO:-$K/drivers/media/i2c/dw9719.ko}	# in-tree since the clean kernel (duo.config); was out of tree
[ -f $D ] && cp $D lib/duo/camera/ && echo "dw9719.ko: copied" || echo "WARNING: no dw9719.ko ($D) - no autofocus"
for m in drivers/leds/led-class-flash drivers/leds/flash/leds-qcom-flash drivers/power/reset/qcom-pon; do
  [ -f $K/$m.ko ] && cp $K/$m.ko lib/duo/ || echo "WARNING: no ${m##*/}.ko"
done
# our panel + touch drivers when the kernel builds them as modules (2026-11-06; /init loads them first thing). With a
# kernel that has them built in there is no .ko and none must be copied (a stale one would fail to load anyway).
rm -f lib/duo/panel-microsoft-surface-duo.ko lib/duo/spi-hid-duo.ko
if grep -q '^CONFIG_DRM_PANEL_MICROSOFT_SURFACE_DUO=m' $K/.config; then cp $K/drivers/gpu/drm/panel/panel-microsoft-surface-duo.ko lib/duo/; echo "panel module: copied"; fi
if grep -q '^CONFIG_SPI_HID_DUO=m' $K/.config; then cp $K/drivers/hid/spi-hid-duo/spi-hid-duo.ko lib/duo/; echo "touch module: copied"; fi
# QMI tools (2026-11-05, GPS): qmi-utils + qrtr from the qmi overlay (make-dev-overlay.sh OVERLAY_BASE="qmi-utils qrtr")
# and our duo-gps (tools/linux/modem/duo-gps.c, built natively on the workbench: see its header) into /usr/local/bin.
QO=${QMI_OVERLAY:-$(dirname "$IN")/../qmi-overlay.tar.gz}
[ -f "$QO" ] && tar -xzf "$QO" && echo "qmi overlay: $(ls usr/bin/qmicli 2>/dev/null)" || echo "WARNING: no qmi overlay ($QO)"
G=${DUO_GPS:-$(dirname "$IN")/../duo-gps}
[ -f "$G" ] && install -m 755 "$G" usr/local/bin/duo-gps && echo "duo-gps: installed" || echo "WARNING: no duo-gps binary ($G)"
aarch64-linux-gnu-gcc -static -O2 -o usr/local/bin/buzz $REPO/tools/linux/buzz/buzz.c && echo "buzz: built" || echo "WARNING: buzz build failed"
# suspend helper (2026-11-04): /usr/local/bin/duo-sleep [seconds]
tr -d "" < $REPO/tools/linux/duo-sleep.sh > usr/local/bin/duo-sleep && chmod 755 usr/local/bin/duo-sleep && echo "duo-sleep: copied"
if [ -d /root/hdr/include ] && command -v aarch64-linux-gnu-gcc >/dev/null; then
  aarch64-linux-gnu-gcc -static -O2 -I/root/hdr/include -o usr/local/bin/drmfill "$REPO/tools/linux/drmfill/drmfill.c"
  echo "drmfill rebuilt"
  for t in v4l2q v4l2enc; do aarch64-linux-gnu-gcc -static -O2 -I/root/hdr/include -o usr/local/bin/$t "$REPO/tools/linux/v4l2q/$t.c" && echo "$t built"; done
fi
# boot splash: the fat workbench penguin (tools/linux/splash), shown by init with drmfill ppm:
mkdir -p usr/share/duo; python3 -I "$REPO/tools/linux/splash/make-penguin-logo.py" usr/share/duo/splash.ppm | sed "s/^/splash: /"
find . | cpio -o -H newc --quiet | gzip -9 > "$OUT"
cd /; rm -rf "$W"; ls -la "$OUT"; md5sum "$OUT"
