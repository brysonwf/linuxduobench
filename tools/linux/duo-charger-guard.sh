#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Run ON the Duo workbench (init starts it at boot; or by hand after loading qcom_smbx): battery temperature guard for
# the PM8150B charger. The PMIC's own thermistor reading is bogus on the Duo, and the charger runs with hardware JEITA
# off (qcom_smbx disable_hw_jeita=1, like Microsoft's sw-jeita), so the limits come from the two bq27742 gauges here:
# outside 0..45 C suspend the charger's USB input (qcom_smbx: status "Unknown" = USBIN suspend), resume at 2..40 C.
# No gauge temperature = suspend. Logs every switch to /dev/kmsg. usage: duo-charger-guard.sh [poll-seconds=10]
# FIX 2026-11-07: pick the CHARGER = a USB-type supply with a WRITABLE status. On pmOS the Type-C port controller
# (tcpm-source-psy-...typec) is also POWER_SUPPLY_TYPE=USB and came first, so the guard watched it and its suspend
# writes went nowhere (no status file) - the temperature cut-off never worked there.
# USB INPUT LIMIT (2026-11-07): on a PC port the PMIC's BC1.2 detection never completes while the USB gadget owns the
# data lines (usb_type -EAGAIN) and the input stayed at the ~100 mA fallback -> the phone DISCHARGED on USB. Once the
# gadget is CONFIGURED by the host the USB spec allows 500 mA (USB 2) / 900 mA (USB 3); the guard writes that to
# current_max (qcom_smbx patched to set ICL override on that write: patches/qcom-smbx-icl-override.py).
S=${1:-10}
glog() { echo "duo-linux charger-guard: $*" > /dev/kmsg; }
find_charger() {
  # by the 'type' file: pm8150b-charger's uevent has no POWER_SUPPLY_TYPE line on pmOS (only the Type-C psy does)
  for d in /sys/class/power_supply/*; do
    [ "$(cat $d/type 2>/dev/null)" = USB ] && [ -w $d/status ] && { echo $d; return; }
  done
}
for i in $(seq 1 60); do C=$(find_charger); [ -n "$C" ] && break; sleep 2; done
[ -n "$C" ] || { glog "no charger power supply: not running"; exit 0; }
glog "watching $(basename $C) every ${S}s"; held=0; icl=""
usb_allowance() {   # 500000 / 900000 uA when the gadget is configured and charger detection never finished, else empty
  cat $C/usb_type > /dev/null 2>&1 && return            # detection worked: the driver set the limit itself
  [ "$(cat /sys/class/udc/*/state 2>/dev/null | head -1)" = configured ] || return
  case "$(cat /sys/class/udc/*/current_speed 2>/dev/null | head -1)" in super-speed*) echo 900000;; *) echo 500000;; esac
}
# ANDROID CHARGE PROFILE (2026-11-07, Android parity item 3) - OPT-IN: only when /etc/duo/charge-profile says "android".
# Microsoft's own DT (dtbo overlay 31), software JEITA + step charging, needs qcom_smbx with
# patches/qcom-smbx-fcc-fv-props.py (writable constant_charge_current/_voltage, capped 3.7 A / 4.40 V in the driver):
#   jeita-fcc/fv-ranges: 0..10 C 2.5 A / 4.25 V; 10..40 C 3.6 A / 4.35 V; 40..45 C 2.5 A / 4.25 V (outside: suspended above)
#   step-chg-ranges (battery voltage): < 3.8 V 5.4 A (no extra limit); 3.8..4.3 V 3.6 A; >= 4.3 V 2.5 A
# FCC = min(JEITA, step). The USB input limit still caps what is actually drawn (500 mA on a PC port).
fcc_set=""; fv_set=""
# THERMAL CHARGING (2026-11-07, Microsoft's thermal-engine VTS_THERMAL_CHARGING on its skin sensor): the hotter of the
# board's skin thermistors (DT zones skin-therm-thermal / msm-therm-thermal, sm8150-microsoft-surface-duo-thermal.dtsi)
# steps a level up at 46.5 / 49.5 / 53.5 C and down below 44.5 / 47.5 / 51.5 C; levels 1..3 cap the charge current at
# Microsoft's thermal-mitigation values 1.85 / 0.74 / 0.185 A (dtbo overlay 31). No skin zones (older DTB) = level 0.
tlvl=0
skin_temp() {   # milli-C, the hotter of the two; empty when the DT has no skin zones
  m=""; for z in /sys/class/thermal/thermal_zone*; do
    case "$(cat $z/type 2>/dev/null)" in skin-therm-thermal|msm-therm-thermal)
      t=$(cat $z/temp 2>/dev/null) || continue; { [ -z "$m" ] || [ "$t" -gt "$m" ]; } && m=$t;; esac; done; echo "$m"
}
thermal_level() {
  st=$(skin_temp); [ -n "$st" ] || { tlvl=0; return; }
  o=$tlvl
  case $tlvl in
    0) [ "$st" -ge 46500 ] && tlvl=1 ;;
    1) if [ "$st" -ge 49500 ]; then tlvl=2; elif [ "$st" -lt 44500 ]; then tlvl=0; fi ;;
    2) if [ "$st" -ge 53500 ]; then tlvl=3; elif [ "$st" -lt 47500 ]; then tlvl=1; fi ;;
    3) [ "$st" -lt 51500 ] && tlvl=2 ;;
  esac
  [ $tlvl != $o ] && glog "skin ${st} mC: thermal charging level $o -> $tlvl"
}
thermal_cap() { case $tlvl in 1) echo 1850000;; 2) echo 740000;; 3) echo 185000;; *) echo "";; esac; }
default_profile() {   # not the android profile: the driver's DT maximum, capped by the thermal level
  [ -w $C/constant_charge_current ] || return
  mx=$(cat $C/constant_charge_current_max 2>/dev/null); [ -n "$mx" ] || return
  fcc=$mx; c=$(thermal_cap); [ -n "$c" ] && [ "$c" -lt "$fcc" ] && fcc=$c
  if [ "$fcc" != "$fcc_set" ]; then echo $fcc > $C/constant_charge_current && fcc_set=$fcc && glog "charge current ${fcc} uA (thermal level $tlvl)"; fi
}
# CHARGE LIMIT (2026-11-07, opt-in): /etc/duo/charge-limit = a percent 50..99 (e.g. 80). At the limit the charge
# current goes to 0 - the phone keeps running from USB, the battery is held (no input suspend, which would drain it);
# charging resumes 5 % below. Average of the two gauges. Remove the file (or write 100) to charge to full again.
limited=0; fcc_before=""
unlimit() {   # back to the charge current from before the pause (the profiles may change it afterwards)
  limited=0; fcc_set=""; [ -n "$fcc_before" ] && [ "$fcc_before" != 0 ] && echo $fcc_before > $C/constant_charge_current
  glog "$1: charging resumed ($(cat $C/constant_charge_current) uA)"
}
limit_check() {
  L=$(cat /etc/duo/charge-limit 2>/dev/null)
  case "$L" in [5-9][0-9]) ;; *) [ $limited = 1 ] && unlimit "charge limit off"; return;; esac
  cap=0; n=0; for g in /sys/class/power_supply/bq27742-*; do v=$(cat $g/capacity 2>/dev/null) || continue; cap=$((cap + v)); n=$((n + 1)); done
  [ $n -gt 0 ] || return; cap=$((cap / n))
  if [ $limited = 0 ] && [ $cap -ge $L ]; then
    fcc_before=$(cat $C/constant_charge_current); limited=1; glog "battery ${cap}% >= limit ${L}%: charging paused (running from USB)"
  elif [ $limited = 1 ] && [ $cap -le $((L - 5)) ]; then unlimit "battery ${cap}% <= $((L - 5))% (limit ${L}%)"; fi
  [ $limited = 1 ] && [ "$fcc_set" != 0 ] && echo 0 > $C/constant_charge_current && fcc_set=0
}
# AUTO-RECHARGE (2026-11-08, Android parity item 3: Microsoft's DT auto-recharge-soc = 95): once the charger reports
# Full, no top-ups until the battery is down to 95 % (average of the two gauges) - the same pause as the charge limit
# (charge current 0, the phone runs from USB). A charge-limit file takes precedence (limited=1); this is limited=2.
recharge_check() {
  if [ $limited = 0 ] && [ "$(cat $C/status 2>/dev/null)" = Full ]; then
    fcc_before=$(cat $C/constant_charge_current); limited=2; glog "charge complete: no top-up until 95 % (Android auto-recharge)"
  fi
  [ $limited = 2 ] || return
  cap=0; n=0; for g in /sys/class/power_supply/bq27742-*; do v=$(cat $g/capacity 2>/dev/null) || continue; cap=$((cap + v)); n=$((n + 1)); done
  [ $n -gt 0 ] || return; cap=$((cap / n))
  if [ $cap -le 95 ]; then unlimit "battery ${cap}% <= 95% after a full charge"; return; fi
  [ "$fcc_set" != 0 ] && echo 0 > $C/constant_charge_current && fcc_set=0
}
android_profile() {
  [ "$(cat /etc/duo/charge-profile 2>/dev/null)" = android ] || return
  [ -w $C/constant_charge_current ] || return
  if [ "$lo" -lt 100 ] || [ "$hi" -gt 400 ]; then fcc=2500000; fv=4250000; else fcc=3600000; fv=4350000; fi
  vb=0; for g in /sys/class/power_supply/bq27742-*; do v=$(cat $g/voltage_now 2>/dev/null) || continue; [ "$v" -gt "$vb" ] && vb=$v; done
  if [ "$vb" -ge 4300000 ]; then s=2500000; elif [ "$vb" -ge 3800000 ]; then s=3600000; else s=$fcc; fi
  [ "$s" -lt "$fcc" ] && fcc=$s
  c=$(thermal_cap); [ -n "$c" ] && [ "$c" -lt "$fcc" ] && fcc=$c
  if [ "$fv" != "$fv_set" ]; then echo $fv > $C/constant_charge_voltage && fv_set=$fv && glog "android profile: float ${fv} uV (battery ${lo}..${hi} dC)"; fi
  if [ "$fcc" != "$fcc_set" ]; then echo $fcc > $C/constant_charge_current && fcc_set=$fcc && glog "android profile: charge current ${fcc} uA (battery ${lo}..${hi} dC, ${vb} uV)"; fi
}
while sleep $S; do
  if [ $held = 0 ] && [ "$(cat $C/online 2>/dev/null)" = 1 ]; then
    a=$(usb_allowance)
    [ -n "$a" ] && [ "$a" != "$icl" ] && echo $a > $C/current_max 2>/dev/null && { icl=$a; glog "USB configured, detection pending: input limit $((a / 1000)) mA"; }
  else icl=""; fi
  hi=-999; lo=999
  for g in /sys/class/power_supply/bq27742-*; do t=$(cat $g/temp 2>/dev/null) || continue
    [ "$t" -gt "$hi" ] && hi=$t; [ "$t" -lt "$lo" ] && lo=$t; done
  if [ "$hi" = -999 ]; then
    [ $held = 0 ] && { echo Unknown > $C/status; held=1; glog "no gauge temperature: input suspended"; }
  elif [ $held = 0 ] && { [ "$hi" -gt 450 ] || [ "$lo" -lt 0 ]; }; then
    echo Unknown > $C/status; held=1; glog "battery ${lo}..${hi} dC out of 0..45 C: input suspended"
  elif [ $held = 1 ] && [ "$hi" -le 400 ] && [ "$lo" -ge 20 ]; then
    echo Charging > $C/status; held=0; glog "battery ${lo}..${hi} dC: charging resumed"
  fi
  thermal_level
  if [ $held = 0 ] && [ "$hi" != -999 ]; then
    limit_check
    [ $limited = 1 ] || recharge_check
    if [ $limited != 0 ]; then :
    elif [ "$(cat /etc/duo/charge-profile 2>/dev/null)" = android ]; then android_profile; else default_profile; fi
  fi
done
