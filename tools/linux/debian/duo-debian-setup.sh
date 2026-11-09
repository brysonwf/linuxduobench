#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# duo-debian-setup - runs INSIDE the Debian tree (chroot) and makes it a Duo system: packages (Mesa from backports,
# the Plasma Mobile pieces, our firewall/keyboard apps when sent), the auto-login user, sddm, our system layer units.
# Shared by the dev prepare (duo-debian-prepare.sh, from the PC) and the on-demand installer (installer.md "Slim
# installer"). Optional inputs in /tmp: opensnitch-rules.sh, waydroid-nft.conf, duo-gps.c, duo-debs/, porchlight/,
# hingeboard/. apt uses whatever proxy the caller configured (installer: socks5h through the PC's SSH tunnel).
set -e
export PATH=/usr/sbin:/usr/bin:/sbin:/bin DEBIAN_FRONTEND=noninteractive LANG=C
apt-get -q update > /dev/null 2>&1 || true
apt-get -q -y -t trixie-backports install libegl-mesa0 libgbm1 libgl1-mesa-dri libglx-mesa0 mesa-libgallium \
  mesa-vulkan-drivers > /tmp/apt-mesa.log 2>&1 || tail -3 /tmp/apt-mesa.log
apt-get -q -y --no-install-recommends install sddm hexagonrpcd iio-sensor-proxy modemmanager rmtfs tqftpserv \
  protection-domain-mapper qrtr-tools umtp-responder avahi-daemon alsa-ucm-conf python3-dbus python3-gi \
  netcat-openbsd psmisc libqmi-utils iproute2 qbootctl gdisk accountsservice \
  kmod wpasupplicant bluez systemd-timesyncd ca-certificates systemd-coredump nftables musl \
  bluedevil hfd-service kde-cli-tools kio-extras ksystemstats systemsettings \
  opensnitch python3-opensnitch-ui qtwayland5 chromium gnome-snapshot > /tmp/apt.log 2>&1 \
  || { tail -5 /tmp/apt.log; exit 1; }
id user > /dev/null 2>&1 || useradd -m -u 10000 -s /bin/bash -G audio,video,input,render,plugdev,netdev,dialout,sudo user
passwd -d user > /dev/null
S=$(ls /usr/share/wayland-sessions | grep -m1 -i plasma-mobile || ls /usr/share/wayland-sessions | grep -m1 -i plasma)
mkdir -p /etc/sddm.conf.d
printf '[Autologin]\nUser=user\nSession=%s\n\n[General]\nDisplayServer=wayland\n' "$S" > /etc/sddm.conf.d/10-duo-autologin.conf
echo duo-debian > /etc/hostname
# OpenSnitch = the allow/deny pop-up firewall (2026-11-08; its UI autostarts in the session, qtwayland5 lets the Qt5 UI
# open on Wayland). Base rules (loopback, DNS, clock, NetworkManager, ModemManager, avahi, apt) never wait for a pop-up;
# the process monitor is /proc (this kernel has no syscall tracepoints for its eBPF monitor).
[ -s /tmp/opensnitch-rules.sh ] && sh /tmp/opensnitch-rules.sh /etc/opensnitchd/rules && rm -f /tmp/opensnitch-rules.sh
sed -i 's/"ProcMonitorMethod": "ebpf"/"ProcMonitorMethod": "proc"/' /etc/opensnitchd/default-config.json 2>/dev/null
systemctl enable opensnitch > /dev/null 2>&1 || true
[ -f /tmp/porchlight/install.sh ] && sh /tmp/porchlight/install.sh && rm -rf /tmp/porchlight
if [ -f /tmp/hingeboard/install.sh ]; then
  apt-get -q -y --no-install-recommends install python3-gi python3-gi-cairo python3-cairo python3-evdev gir1.2-gtk-4.0 \
    gir1.2-gtk4layershell-1.0 libgtk4-layer-shell0 wlr-randr opendoas labwc lxqt-panel pcmanfm-qt wl-mirror \
    gvfs playerctl lxqt-powermanagement nm-tray blueman > /tmp/apt-hb.log 2>&1 || tail -3 /tmp/apt-hb.log
  sh /tmp/hingeboard/install.sh && rm -rf /tmp/hingeboard
fi
# sensors (libssc + iio-sensor-proxy with SSC, backported from sid) when the .debs were sent
if ls /tmp/duo-debs/*.deb > /dev/null 2>&1; then apt-get -q -y install /tmp/duo-debs/*.deb > /tmp/apt-ssc.log 2>&1 || tail -3 /tmp/apt-ssc.log; rm -rf /tmp/duo-debs; fi
# VS Code: native Wayland (Electron apps follow ELECTRON_OZONE_PLATFORM_HINT) and no keyring prompt (verified 2026-11-08:
# VS Code 1.145.2 opens on the Duo; an auto-login user has no keyring password)
apt-get -q update > /dev/null 2>&1 && apt-get -q -y install code > /tmp/apt-code.log 2>&1 || tail -3 /tmp/apt-code.log
grep -q ELECTRON_OZONE_PLATFORM_HINT /etc/environment || echo ELECTRON_OZONE_PLATFORM_HINT=auto >> /etc/environment
mkdir -p /home/user/.vscode
[ -f /home/user/.vscode/argv.json ] || printf '{\n  "password-store": "basic"\n}\n' > /home/user/.vscode/argv.json
chown -R user:user /home/user/.vscode
# Bluetooth firmware (2026-11-08: without it hci0 never finished setup, "Powered: no"): ONLY qca/crbtfw21.tlv + crnv21.bin
# out of firmware-atheros - the whole package also brings the generic ath10k WCN3990 board-2.bin that crashes the Duo's
# Wi-Fi (WCSS), so it is never installed
if [ ! -f /lib/firmware/qca/crbtfw21.tlv ]; then
  (cd /tmp && rm -rf fwa && mkdir fwa && cd fwa && apt-get -q download firmware-atheros > /dev/null 2>&1 &&
   dpkg-deb -x firmware-atheros_*.deb x && mkdir -p /lib/firmware/qca &&
   cp x/usr/lib/firmware/qca/crbtfw21.tlv* x/usr/lib/firmware/qca/crnv21.bin* /lib/firmware/qca/ && echo "bluetooth firmware: crbtfw21 + crnv21")
  rm -rf /tmp/fwa
fi
# NO system suspend (2026-11-08): Plasma's PowerDevil suspended the phone on idle (s2idle) - USB to the PC dropped and
# the watchdog pauses in suspend, so it looked exactly like a freeze (the "01:58 freeze" was this; nothing woke it).
# pmOS sleeps only through our duo-sleep; on Debian the sleep targets are masked until that is ported.
for t in sleep.target suspend.target hibernate.target hybrid-sleep.target suspend-then-hibernate.target; do
  ln -sf /dev/null /etc/systemd/system/$t; done
# and no lock screen: the auto-login user has no password (no PIN to remember on a single-user phone)
mkdir -p /home/user/.config
printf '[Daemon]\nAutolock=false\nLockOnResume=false\n' > /home/user/.config/kscreenlockerrc
# no KDE Wallet: nothing here needs it (system Wi-Fi profiles, VS Code and
# Chromium use the basic password store), and its first-use dialog only got in the way
printf '[Wallet]\nEnabled=false\nFirst Use=false\n' > /home/user/.config/kwalletrc
# Plasma Mobile's first-start wizard (Wi-Fi, time, cellular, navigation) came back on every boot until it is tapped
# through; this setup already did all of it -> mark it run ([InitialStart] wizardRun in plasmamobilerc, 6.3.6)
MRC=/home/user/.config/plasmamobilerc; touch $MRC
grep -q '^\[InitialStart\]' $MRC || printf '\n[InitialStart]\nwizardRun=true\n' >> $MRC
# Waydroid (Android apps, 2026-11-08: "Android with user 0 is ready"): the repo + nft networking (our kernel has no
# legacy iptables); images go to sda10 /waydroid (waydroid init -i /mnt/duo-part/waydroid/images, once, needs Wi-Fi)
apt-get -q -y install waydroid > /tmp/apt-wd.log 2>&1 || tail -3 /tmp/apt-wd.log
mkdir -p /etc/systemd/system/waydroid-container.service.d && mv /tmp/waydroid-nft.conf /etc/systemd/system/waydroid-container.service.d/duo-nft.conf
# touch under KWin (2026-11-08): KWin ignores the wlroots WL_OUTPUT property and mis-applied our udev
# LIBINPUT_CALIBRATION_MATRIX (dropped one translation: taps landed above the screen). Its own per-device settings
# fix both: the digitizer (landscape 9600x7200) -> portrait panel = x'=1-y, y'=1-x (row-major 4x4 below, the same
# matrix as pmOS), 0C01 = DSI-2 (camera side), 0C02 = DSI-1. Measured by injected taps (KWin logs off-screen points).
mkdir -p /home/user/.config
cat > /home/user/.config/kcminputrc << 'KCM'
[Libinput][1118][3073][IPTSD Virtual Touchscreen 045E:0C01]
CalibrationMatrix=0,-1,1,0,-1,0,1,0,0,0,1,0,0,0,0,1
OutputName=DSI-2

[Libinput][1118][3074][IPTSD Virtual Touchscreen 045E:0C02]
CalibrationMatrix=0,-1,1,0,-1,0,1,0,0,0,1,0,0,0,0,1
OutputName=DSI-1
KCM
chown -R user:user /home/user/.config
# /mnt/linux = sda10 as pmOS sees it (iptsd, hexagonrpcd under /mnt/linux/opt/bin): here sda10 is already our root
# (/init keeps the whole partition at /mnt/duo-part), and a second ro mount is refused -> bind it
mkdir -p /mnt/linux
grep -q ' /mnt/linux ' /etc/fstab 2>/dev/null || echo '/mnt/duo-part /mnt/linux none bind 0 0' >> /etc/fstab
mkdir -p /etc/NetworkManager/conf.d
printf '[device-usb0]\nmatch-device=interface-name:usb0\nmanaged=false\n' > /etc/NetworkManager/conf.d/50-duo-usb0.conf
# usb0 = the PC link; our initramfs already gave it 169.254.66.1 + 10.66.0.1 - nothing to do here
# our patched hexagonrpcd on sda10 (/opt/bin) is a musl build (pmOS): Debian's musl package gives the loader, and
# Alpine names the libc libc.musl-aarch64.so.1 - the same file (2026-11-08: duo-sensors ran once this existed)
[ -e /lib/libc.musl-aarch64.so.1 ] || ln -s ld-musl-aarch64.so.1 /lib/libc.musl-aarch64.so.1
ln -sf /usr/bin/umtprd /usr/local/sbin/umtprd    # duo-mtp calls our musl build there; Debian packages it (glibc, /usr/bin)
# duo-gps (our QMI LOC client, tools/linux/modem/duo-gps.c) built here for glibc; then duo-gps-assist can run
if [ -s /tmp/duo-gps.c ]; then
  apt-get -q -y --no-install-recommends install gcc libc6-dev pkg-config libqmi-glib-dev > /tmp/apt-gps.log 2>&1 &&
    gcc -O2 -o /usr/local/bin/duo-gps /tmp/duo-gps.c $(pkg-config --cflags --libs qmi-glib) &&
    systemctl enable duo-gps-assist > /dev/null 2>&1 && echo "built /usr/local/bin/duo-gps (glibc)"
  rm -f /tmp/duo-gps.c
fi
rm -f /usr/sbin/policy-rc.d
systemctl enable nftables ssh sddm NetworkManager duo-hw duo-watchdog duo-autobrightness duo-bootanim duo-bootanim-done \
  duo-charger duo-bt-addr duo-sensors duo-modem duo-mtp > /dev/null 2>&1
# (the slot self-recovery is not in the source-only public release: only when its unit exists)
[ -f /etc/systemd/system/duo-boot-check.timer ] && systemctl enable duo-boot-check.timer > /dev/null 2>&1
# self-recovery: the slot is marked good only by duo-boot-check (45 s, healthy); qbootctl's own "mark at once" unit is
# masked (installer.md "Self-recovery")
[ -f /etc/systemd/system/duo-boot-check.timer ] && { systemctl mask qbootctl.service > /dev/null 2>&1 || true; }
# ModemManager: started by duo-modem once the modem is in low-power (never straight online - the QLink crash)
systemctl disable ModemManager > /dev/null 2>&1 || true
# as pmOS: rmtfs + tqftpserv on; the pd-mapper daemon and qrtr-ns OFF (both are in the kernel; the daemons conflict)
systemctl disable pd-mapper qrtr-ns > /dev/null 2>&1 || true
# Debian's own hexagonrpcd unit: off - duo-sensors starts ours (static, sda10 /opt/bin) with the Duo's arguments
systemctl disable hexagonrpcd > /dev/null 2>&1 || true
chown -R root:root /lib/duo
echo "setup done: session=$S, user=$(id -u user), enabled: $(systemctl list-unit-files 'duo-*' --state=enabled --no-legend | wc -l) duo units"
