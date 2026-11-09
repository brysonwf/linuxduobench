#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the Duo memory-boot workbench initramfs: Alpine minirootfs (aarch64) + workbench packages (deps resolved by
# apk-resolve.py) + our tools (devmem, drmfill) + /init + optional ADSP firmware + an SSH public key for root.
# Everything runs from RAM; nothing on the phone is touched unless you do it from the shell.
# usage (WSL root): make-initramfs.sh <repo-root> <out.cpio.gz> [adsp.mbn] [ssh-pubkey]
set -e
REPO=$1; OUT=$2; FW=$3; KEY=$4; L=$REPO/tools/linux
W=/root/duo-initramfs; C=/root/alpine-cache; rm -rf $W; mkdir -p $W/root $C
AL=https://dl-cdn.alpinelinux.org/alpine/v3.22
cd $C
MR=$(curl -s $AL/releases/aarch64/latest-releases.yaml | grep -o 'alpine-minirootfs-[0-9.]*-aarch64.tar.gz' | head -1)
[ -f "$MR" ] || curl -sO $AL/releases/aarch64/$MR
for r in main community; do curl -s $AL/$r/aarch64/APKINDEX.tar.gz | tar -xzO APKINDEX > APKINDEX.$r; done
cd $W/root; tar -xzf $C/$MR
awk -F: '$1=="P"{print $2}' lib/apk/db/installed > $C/installed.txt
PKGS="alsa-utils dropbear dropbear-scp e2fsprogs e2fsprogs-extra sgdisk dosfstools ntfs-3g-progs lsblk blkid sfdisk
      util-linux-misc zstd pciutils usbutils i2c-tools evtest strace file less nano htop tmux
      libgpiod openssh-sftp-server
      iw wpa_supplicant wireless-regdb linux-firmware-ath10k eudev-libs
      linux-firmware-qca bluez bluez-btmgmt dbus libssc hexagonrpcd"
python3 -I $L/apk-resolve.py $C/APKINDEX.main:main $C/APKINDEX.community:community -- $PKGS --installed $C/installed.txt \
  > $C/resolved.txt
echo "packages: $(wc -l < $C/resolved.txt)"
while read repo f; do
  [ -f $C/$f ] || curl -sfo $C/$f $AL/$repo/aarch64/$f || echo "download failed: $f"
  tar -xzf $C/$f 2>/dev/null || true
done < $C/resolved.txt
# Qualcomm modem helpers that v3.22 lacks, taken by name from Alpine edge/community (musl, small, few deps):
# rmtfs (modem remote file system), tqftpserv (serves wlanmdsp.mbn etc. to the modem), qrtr-libs (their libqrtr).
EDGE=https://dl-cdn.alpinelinux.org/alpine/edge/community
[ -f $C/APKINDEX.edge ] || curl -s $EDGE/aarch64/APKINDEX.tar.gz | tar -xzO APKINDEX > $C/APKINDEX.edge
for p in rmtfs tqftpserv qrtr-libs; do
  v=$(awk -v p="P:$p" '$0==p{f=1} f&&/^V:/{print substr($0,3); exit}' $C/APKINDEX.edge)
  f=$p-$v.apk; [ -f $C/$f ] || curl -sfo $C/$f $EDGE/aarch64/$f || echo "download failed: $f"
  tar -xzf $C/$f 2>/dev/null || true; echo "edge: $f"
done
rm -f .PKGINFO .SIGN.* .pre-* .post-* .trigger
# our tools (static), into /usr/local/bin
mkdir -p usr/local/bin  # (also symlinked into /usr/bin below: dropbear sessions do not get /usr/local/bin on PATH)
aarch64-linux-gnu-gcc -static -O2 -o usr/local/bin/devmem $L/devmem/devmem.c
[ -d /root/hdr/include ] || make -C /root/linux ARCH=arm64 headers_install INSTALL_HDR_PATH=/root/hdr >/dev/null
aarch64-linux-gnu-gcc -static -O2 -I/root/hdr/include -o usr/local/bin/drmfill $L/drmfill/drmfill.c
ln -sf /usr/local/bin/devmem usr/bin/devmem; ln -sf /usr/local/bin/drmfill usr/bin/drmfill
tr -d '\r' < $L/initramfs/init > init; chmod +x init
tr -d '\r' < $L/initramfs/audiotest.sh > audiotest.sh; chmod +x audiotest.sh
if [ -n "$FW" ]; then mkdir -p lib/firmware/qcom/sm8150/microsoft; cp "$FW" lib/firmware/qcom/sm8150/microsoft/adsp.mbn; fi
if [ -n "$KEY" ]; then mkdir -p root/.ssh etc/dropbear; tr -d '\r' < "$KEY" > root/.ssh/authorized_keys
  chmod 700 root/.ssh; chmod 600 root/.ssh/authorized_keys; fi
echo duo > etc/hostname
for t in usr/sbin/dropbear usr/bin/rmtfs usr/bin/tqftpserv usr/sbin/iw sbin/wpa_supplicant lib/firmware/ath10k/WCN3990/hw1.0/firmware-5.bin.zst usr/bin/speaker-test sbin/mkfs.ext4 usr/bin/sgdisk usr/sbin/resize2fs usr/local/bin/devmem usr/local/bin/drmfill bin/busybox; do [ -e $t ] && echo "ok $t" || echo "MISSING $t"; done
find . | cpio -o -H newc 2>/dev/null | gzip -9 > "$OUT"; ls -la "$OUT"
