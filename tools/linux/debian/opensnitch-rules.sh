#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenSnitch (the allow/deny pop-up firewall)
# base rules for the Duo on Debian: everything NOT listed here asks with a pop-up (OpenSnitch's UI, unanswered = deny).
# These few must never wait for a pop-up nobody answers on an unattended phone:
#   - loopback: our watchdog's health check (nc to our own sshd) - denied 60 s = a watchdog reset
#   - DNS (port 53): otherwise every app's first lookup is its own pop-up before the real connection's
#   - time sync, NetworkManager (connectivity check), ModemManager, avahi (duo-debian.local), apt's downloaders
# usage (on the phone, as root): sh opensnitch-rules.sh [rules dir, default /etc/opensnitchd/rules]
D=${1:-/etc/opensnitchd/rules}; mkdir -p "$D"
T=$(date -u +%Y-%m-%dT%H:%M:%S.000000000Z)
rule() {   # name operand data description
	cat > "$D/$1.json" << EOF
{
  "created": "$T",
  "updated": "$T",
  "name": "$1",
  "description": "$4",
  "enabled": true,
  "precedence": true,
  "nolog": false,
  "action": "allow",
  "duration": "always",
  "operator": {"type": "$( case $2 in dest.network) echo network;; process.command) echo regexp;; *) echo simple;; esac )", "operand": "$2", "sensitive": false, "data": "$3", "list": []}
}
EOF
}
rule 000-duo-allow-localhost4 dest.network 127.0.0.0/8 "Duo: loopback (watchdog health check to our own sshd)"
rule 000-duo-allow-localhost6 dest.network ::1/128 "Duo: loopback (IPv6)"
rule 001-duo-allow-dns dest.port 53 "Duo: DNS lookups (the app's real connection still asks)"
rule 002-duo-allow-timesyncd process.path /usr/lib/systemd/systemd-timesyncd "Duo: clock (NTP)"
rule 002-duo-allow-networkmanager process.path /usr/sbin/NetworkManager "Duo: NetworkManager connectivity check"
rule 002-duo-allow-modemmanager process.path /usr/sbin/ModemManager "Duo: ModemManager"
rule 002-duo-allow-avahi process.path /usr/sbin/avahi-daemon "Duo: mDNS (duo-debian.local)"
rule 003-duo-allow-apt-http process.path /usr/lib/apt/methods/http "Duo: apt downloads (http)"
rule 003-duo-allow-apt-https process.path /usr/lib/apt/methods/https "Duo: apt downloads (https)"
# Waydroid (Android apps): its tools are python (process.path = the interpreter), so match the command line. It checks
# its OTA index on every init/upgrade; denied, the SYN just hangs (init stuck in SYN-SENT, 2026-11-08)
rule 004-duo-allow-dnsmasq process.path /usr/sbin/dnsmasq "Duo: Waydroid's DHCP/DNS for Android (bridge waydroid0)"
# the DHCP offers to Android are not tied to dnsmasq by OpenSnitch (broadcast): allow the bridge net by destination
# (bisected 2026-11-08: Android got no address with OpenSnitch running, 192.168.240.112 at once without it)
rule 004-duo-allow-waydroid-net dest.network 192.168.240.0/24 "Duo: Waydroid bridge (Android's DHCP/DNS)"
rule 004-duo-allow-dhcp-broadcast dest.network 255.255.255.255/32 "Duo: DHCP broadcast replies"
rule 004-duo-allow-waydroid process.command '^/usr/bin/python3 /usr/bin/waydroid' "Duo: Waydroid image updates"
echo "OpenSnitch base rules in $D: $(ls "$D" | grep -c '^00[0-3]-duo-')"
