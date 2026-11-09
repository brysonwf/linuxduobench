#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Make the Surface Duo speaker's DSP tuning from the phone's OWN calibration database (ACDB on vendor_a) for the
# kernel (sm8150-microsoft-surface-duo-audio.dtsi): avcs-topologies.bin (custom topologies), speaker-copp.bin (mono
# MBDRC chain + gain), speaker-afe.bin (feed-forward speaker protection). Nothing from the phone is shipped: the files
# are made on the device and stay there. Without them the kernel plays the speaker untuned.
# Region: Microsoft ships region a/b tunings; the bootloader names the phone's one (androidboot.hardware.audio.tuning
# on ABL boots), else /etc/duo/audio-tuning, else b. Makeup gain +8 dB (listening test, adsp-calibration.md).
# usage: duo-speaker-tuning.sh [vendor_root=/mnt/vendor-fw] [out=/lib/firmware/qcom/sm8150/microsoft]
# exit 0 = files present (made now or before), 1 = no ACDB / tools failed (kernel stays untuned)
V=${1:-/mnt/vendor-fw}; O=${2:-/lib/firmware/qcom/sm8150/microsoft}
T=/usr/local/share/duo/audio-tuning; [ -f $T/acdb-cal.py ] || T=$(dirname "$(readlink -f "$0")")
FILES="avcs-topologies.bin speaker-copp.bin speaker-afe.bin"
ok() { for f in $FILES; do [ -s "$O/$f" ] || return 1; done; }
ok && exit 0

R=$(tr ' ' '\n' < /proc/cmdline | sed -n 's/^androidboot.hardware.audio.tuning=//p')
[ -n "$R" ] || R=$(cat /etc/duo/audio-tuning 2>/dev/null)
case "$R" in a|b) ;; *) R=b ;; esac
U=$(echo $R | tr a-z A-Z)
A=$V/etc/acdbdata/Surface/region/$R
[ -f $A/SURFACE_${U}_Speaker_cal.acdb ] && [ -f $A/SURFACE_${U}_Global_cal.acdb ] || { echo "duo-speaker-tuning: no ACDB in $A"; exit 1; }

tmp=$(mktemp -d) || exit 1
python3 -I $T/acdb-topologies.py $A/SURFACE_${U}_Global_cal.acdb --avcs-blob $tmp/avcs-topologies.bin > /dev/null &&
python3 -I $T/acdb-cal.py $A/SURFACE_${U}_Speaker_cal.acdb 14 --gain-db 8 \
	--copp-blob $tmp/speaker-copp.bin --afe-blob $tmp/speaker-afe.bin > /dev/null &&
	mkdir -p $O && for f in $FILES; do [ -s $tmp/$f ] && mv $tmp/$f $O/$f; done
rm -rf $tmp
ok && { echo "duo-speaker-tuning: region $R tuning in $O"; exit 0; }
echo "duo-speaker-tuning: failed (region $R)"; exit 1
