#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Surface Duo on postmarketOS: is everything we rely on still in place? (2026-11-07) Run as root ON the phone:
#   duo-health            (installed by duo-pmos-prepare.sh as /usr/local/sbin/duo-health)
# One line per check, PASS / WARN / FAIL, exit code = number of FAILs. Read-only: it changes nothing.
# Catches what an apk upgrade or a half-done install silently undoes (stock libssc back, no module set for the
# running kernel, firewall drop-in gone, ...) and what a fresh installer needs to confirm.
fails=0
ok()   { echo "PASS  $*"; }
warn() { echo "WARN  $*"; }
bad()  { echo "FAIL  $*"; fails=$((fails + 1)); }
zone() { for z in /sys/class/thermal/thermal_zone*; do [ "$(cat $z/type 2>/dev/null)" = "$1" ] && { echo $z; return; }; done; }
R=$(uname -r)

# --- system
[ "$(stat -c %a /)" = 755 ] && ok "/ is 755" || bad "/ is $(stat -c %a /) (must be 755: su/Phosh break)"
[ -c /dev/null ] && ok "/dev/null is a character device" || bad "/dev/null is not a device (user apps fail; mknod -m 666 /dev/null c 1 3)"
[ -d /lib/duo/$R ] && n=$(find /lib/duo/$R -name '*.ko' -size +0 | wc -l) && [ "$n" -ge 20 ] \
  && ok "module set for $R ($n modules)" || bad "no module set /lib/duo/$R for the running kernel (prepare installs duo-mods-$R)"
for m in spi_hid_duo panel_microsoft_surface_duo snd_soc_sdm845 qcom_smbx; do
  grep -q "^$m " /proc/modules && ok "module $m loaded" || bad "module $m not loaded"; done

# --- services
# OpenRC on postmarketOS; systemd on Debian (2026-11-08: the units of tools/linux/system)
if [ "$(cat /proc/1/comm 2>/dev/null)" = systemd ]; then
  for s in duo-hw duo-sensors duo-charger duo-watchdog nftables; do
    st=$(systemctl is-active $s 2>/dev/null); [ "$st" = active ] && ok "service $s active" || bad "service $s: ${st:-missing}"; done
  c=$(systemctl --failed --no-legend 2>/dev/null | grep -c .); [ "$c" = 0 ] && ok "no failed units" || bad "$c failed unit(s): $(systemctl --failed --no-legend | awk '{print $2}' | tr '\n' ' ')"
else
  for s in duo-hw duo-sensors duo-charger duo-watchdog nftables; do
    st=$(rc-service $s status 2>/dev/null | sed 's/.*status: //'); [ "$st" = started ] && ok "service $s started" || bad "service $s: ${st:-missing}"; done
  c=$(rc-status -a 2>/dev/null | grep -c crashed); [ "$c" = 0 ] && ok "no crashed services" || bad "$c crashed service(s): $(rc-status -a | grep crashed | awk '{print $1}' | tr '\n' ' ')"
fi

# --- displays, touch
# a panel the desktop blanked for idle (DPMS Off, still connected) is fine - only a disconnected/dead one fails
for p in DSI-1 DSI-2; do C=$(ls -d /sys/class/drm/card*-$p 2>/dev/null | head -1)
  if [ "$(cat $C/enabled 2>/dev/null)" = enabled ]; then ok "panel $p on"
  elif [ "$(cat $C/status 2>/dev/null)" = connected ] && [ "$(cat $C/dpms 2>/dev/null)" = Off ]; then ok "panel $p blanked (idle)"
  else bad "panel $p off"; fi; done
t=$(grep -ci "^I:.*Vendor=045e" /proc/bus/input/devices); [ "$t" -ge 2 ] && ok "touch: $t digitizer input devices" || bad "touch: digitizer input missing"
if pidof phosh > /dev/null; then ok "Phosh running"; elif pidof plasmashell > /dev/null; then ok "Plasma Mobile running"; else warn "no phone shell running (Phosh / Plasma Mobile; desktop session or not logged in?)"; fi

# --- audio
grep -q SurfaceDuo /proc/asound/cards && ok "sound card Surface-Duo" || bad "no Surface-Duo sound card"
# speaker: Microsoft's DSP tuning (adsp-calibration.md) when its files exist, else PipeWire's stand-in EQ - never both
F=/lib/firmware/qcom/sm8150/microsoft; PW=/etc/pipewire/pipewire.conf.d/60-duo-speaker-tuning.conf
if [ -s $F/avcs-topologies.bin ] && [ -s $F/speaker-copp.bin ] && [ -s $F/speaker-afe.bin ]; then
  [ -f $PW ] && bad "speaker tuned twice: DSP tuning files AND the PipeWire stand-in ($PW)" || ok "speaker DSP tuning files present (PipeWire stand-in off)"
  dmesg | grep -q "custom topologies ([0-9]* bytes): 0" && ok "DSP custom topologies registered" || warn "DSP custom topologies not registered yet (nothing played since boot?)"
else
  [ -f $PW ] && warn "speaker DSP tuning missing: PipeWire stand-in EQ in use" || bad "speaker untuned: no DSP tuning files and no PipeWire stand-in"
fi
j=$(dmesg | grep -ciE "plug type detected|Impedance detect"); [ "$j" = 0 ] && ok "no phantom headset (MBHC off)" || warn "MBHC ran ($j lines): DT without qcom,no-headset-jack?"

# --- mobile data path (IPA v4.1; kernel series 65+)
if [ -f /lib/duo/$R/ipa.ko ]; then
  [ -e /sys/class/net/rmnet_ipa0 ] && ok "IPA: rmnet_ipa0 present" || bad "IPA: no rmnet_ipa0 (duo-modem loads ipa.ko before the modem)"
  mmcli -L 2>/dev/null | grep -q Modem/ && mmcli -m any 2>/dev/null | grep -q "rmnet_ipa0 (net)" \
    && ok "ModemManager: modem with net port rmnet_ipa0" || warn "ModemManager: no modem with rmnet_ipa0 yet (starts ~40 s after boot)"
fi

# --- sensors (light/proximity need OUR libssc)
if [ -s /etc/duo/libssc-patched.md5 ]; then
  [ "$(md5sum /usr/lib/libssc.so.0 | cut -d' ' -f1)" = "$(cat /etc/duo/libssc-patched.md5)" ] \
    && ok "libssc: the patched build" || bad "libssc replaced (apk upgrade?): rerun sensors/duo-libssc-build-pmos.sh, then prepare"
elif v=$(dpkg-query -W -f '${Version}' libssc2 2>/dev/null) && [ -n "$v" ] && dpkg --compare-versions "$v" ge 0.4; then
  ok "libssc $v (0.4+ needs no open_sync patch; Debian backport)"
else warn "libssc: stock (light/proximity stay off; sensors/duo-libssc-build-pmos.sh)"; fi
grep -q ssc-light /etc/udev/rules.d/81-duo-ssc.rules 2>/dev/null && ok "iio-sensor-proxy: light + proximity enabled" || warn "iio-sensor-proxy: accel + compass only"
a=$(timeout 5 gdbus call --system --dest net.hadess.SensorProxy --object-path /net/hadess/SensorProxy --method \
  org.freedesktop.DBus.Properties.Get net.hadess.SensorProxy HasAccelerometer 2>/dev/null)
case "$a" in *true*) ok "iio-sensor-proxy answers (accelerometer)";; *) bad "iio-sensor-proxy does not answer D-Bus";; esac

# --- power, thermal, storage
for g in /sys/class/power_supply/bq27742-*; do t=$(cat $g/temp 2>/dev/null)
  [ -n "$t" ] && [ "$t" -gt 0 ] && [ "$t" -lt 450 ] && ok "$(basename $g) $((t / 10)).$((t % 10)) C" || bad "$(basename $g): temperature ${t:-unreadable}"; done
for z in skin-therm-thermal msm-therm-thermal; do d=$(zone $z)
  if [ -n "$d" ]; then ok "thermal zone $z $(( $(cat $d/temp) / 1000 )) C, trips $(cat $d/trip_point_0_temp)/$(cat $d/trip_point_1_temp)"
  else warn "thermal zone $z missing (DTB without the skin thermal zones)"; fi; done
u=$(dmesg | grep -ciE "ufshcd.*(error|err_|timeout|reset)|pa_err|dl_err|uic.*err"); [ "$u" = 0 ] && ok "UFS: 0 errors" || bad "UFS: $u error lines"
U=/sys/bus/platform/devices/1d84000.ufshc; [ "$(cat $U/clkgate_enable 2>/dev/null)" = 1 ] && ok "UFS power saving on" || warn "UFS power saving off (duo.ufspm=off or an old kernel)"

# --- network, firewall
nft list ruleset 2>/dev/null | grep -q "Duo: inbound" && ok "firewall: Duo allowlist loaded" || bad "firewall: Duo allowlist NOT loaded (cellular-side SSH open?)"
ip -br link show wlan0 2>/dev/null | grep -q UP && ok "Wi-Fi interface up" || warn "Wi-Fi interface down"

echo "--- $fails FAIL(s) (kernel $R)"
exit $fails
