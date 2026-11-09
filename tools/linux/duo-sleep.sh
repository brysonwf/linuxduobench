#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# duo-sleep [seconds]: suspend the Duo workbench (s2idle) safely; with seconds, the PMIC RTC wakes it (kernel #65+:
# rtc-pm8xxx = rtc1), else a wakeup source (power key, charger, ...) does. Installed as /usr/local/bin/duo-sleep.
# Found 2026-11-04 (docs/research/33 "SUSPEND"):
#  - UFS must suspend at spm_lvl 5 (device off + link off): at our default 0 the UFS never answered after resume
#  - the Bluetooth UART (c8c000.serial) is a wakeup source and the WCN3998 chatters on it -> sleeps ended after 1-2 s;
#    its wakeup is turned off here (Bluetooth itself keeps working when awake)
U=/sys/bus/platform/devices/1d84000.ufshc; R=/sys/class/rtc/rtc1
echo 5 > $U/spm_lvl 2>/dev/null
for w in /sys/bus/platform/devices/c8c000.serial/power/wakeup; do [ -f $w ] && echo disabled > $w; done
if [ -n "$1" ]; then [ -e $R/wakealarm ] || { echo "duo-sleep: no RTC wakealarm (need kernel #65+)"; exit 1; }
  echo 0 > $R/wakealarm; echo +$1 > $R/wakealarm; fi
echo "duo-sleep: s2idle${1:+ for $1 s}" > /dev/kmsg
t0=$(cat $R/since_epoch 2>/dev/null); echo freeze > /sys/power/state; rc=$?; t1=$(cat $R/since_epoch 2>/dev/null)   # RTC: the kernel clock stops in s2idle
echo "duo-sleep: back (rc $rc) after $((t1 - t0)) s (RTC)" > /dev/kmsg; echo "duo-sleep: back (rc $rc) after $((t1 - t0)) s (RTC)"
