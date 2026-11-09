#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build hexagonrpcd 0.3.2 (linux-msm/hexagonrpc, the version Alpine 3.22 ships) with our keep-serving patch, NATIVELY
# on the Duo workbench (compiler from the dev overlay: tools/linux/make-dev-overlay.sh). Result goes to sda10
# /opt/bin/hexagonrpcd, which the initramfs sensors block prefers over /usr/bin/hexagonrpcd.
# usage (PC, Git Bash): build-hexagonrpcd.sh <repo-root> <ssh-key> [host=169.254.66.1]
set -e
REPO=$1; KEY=$2; HOST=root@${3:-169.254.66.1}
O="-i $KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
T=$(mktemp -d)
curl -sL https://github.com/linux-msm/hexagonrpc/archive/refs/tags/v0.3.2.tar.gz | tar -xz -C "$T"
W=$(cd "$T" && pwd -W 2>/dev/null || pwd)
MSYS_NO_PATHCONV=1 wsl -d Ubuntu-22.04 -- python3 -I "$(wslpath -a "$REPO" 2>/dev/null || echo /mnt/c${REPO#/c})/tools/linux/patches/hexagonrpcd-keep-serving.py" \
  "$(MSYS_NO_PATHCONV=1 wsl -d Ubuntu-22.04 -- wslpath -a "$W")/hexagonrpc-0.3.2"
tar -C "$T" -czf "$T/src.tgz" hexagonrpc-0.3.2
# the compiler lives in RAM (dev overlay, lost on every reboot): load it first if it is missing
if ! ssh $O $HOST 'command -v cc >/dev/null'; then
  echo "loading the dev overlay (artifacts/local/dev-overlay.tar.gz)"
  scp -O $O "$REPO/artifacts/local/dev-overlay.tar.gz" $HOST:/tmp/ &&
    ssh $O $HOST 'cd / && tar -xzf /tmp/dev-overlay.tar.gz && rm /tmp/dev-overlay.tar.gz'
fi
scp -O $O "$T/src.tgz" $HOST:/tmp/hexagonrpc-src.tgz
ssh $O $HOST 'set -e; command -v cc >/dev/null || { echo "no compiler: extract the dev overlay first"; exit 1; }
  cd /tmp && rm -rf hexagonrpc-0.3.2 && tar -xzf hexagonrpc-src.tgz && cd hexagonrpc-0.3.2
  grep -q "keep serving" hexagonrpcd/listener.c
  cc -O2 -Wall -Wno-unused-parameter -Iinclude -o /tmp/hexagonrpcd hexagonrpcd/*.c libhexagonrpc/*.c
  mkdir -p /mnt/lx && mount /dev/sda10 /mnt/lx && mkdir -p /mnt/lx/opt/bin && cp /tmp/hexagonrpcd /mnt/lx/opt/bin/
  ls -la /mnt/lx/opt/bin/hexagonrpcd; sync; umount /mnt/lx; echo HEXAGONRPCD_BUILD_DONE'
