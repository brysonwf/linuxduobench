#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Duo speaker test under mainline Linux (run by /init when the cmdline has duo.audiotest=1).
# Speaker path = Android mixer_paths_tavil.xml "speaker-mono", translated to mainline wcd934x/wsa881x names:
#   MultiMedia1 -> SLIMBUS_0_RX -> SLIM RX0 (AIF1_PB) -> CDC_IF RX0 -> RX INT7 -> COMP7 -> SpkrLeft (WSA8815)
# Also captures the bring-up for a Windows-side helper: amp + codec register dumps idle vs playing, and the
# regmap write trace from stream start. (Never read regmap 217:250:0:0 — the Slimbus interface device wedges.)
LOG=${1:-/tmp/audiotest.log}
CARD=$(grep -l SurfaceDuo /proc/asound/card*/id 2>/dev/null | head -1 | sed 's#.*/card\([0-9]*\)/id#\1#')
CARD=${CARD:-1}
R=/sys/kernel/debug/regmap; AMP="sdw:0:0:0217:2110:00:3"; CODEC="217:250:1:0"
T=/sys/kernel/tracing; [ -d $T/events ] || T=/sys/kernel/debug/tracing
s() { echo "+ amixer cset $1 = $2"; amixer -c "$CARD" cset "name=$1" "$2" 2>&1 | tail -1; }
dump() { for d in $AMP $CODEC; do echo "== regmap $1 $d"; cat "$R/$d/registers" 2>&1; done; }
{
  echo "== audiotest card=$CARD $(cut -d' ' -f1 /proc/uptime)"
  echo "== controls"; amixer -c "$CARD" controls 2>&1
  echo "== set speaker path"
  s 'SLIMBUS_0_RX Audio Mixer MultiMedia1' 1
  s 'SLIM RX0 MUX' 'AIF1_PB'
  s 'CDC_IF RX0 MUX' 'SLIM RX0'
  s 'RX INT7_1 MIX1 INP0' 'RX0'
  s 'COMP7 Switch' 1
  s 'RX7 Digital Volume' 80
  s 'SpkrLeft COMP Switch' 1
  s 'SpkrLeft BOOST Switch' 1
  s 'SpkrLeft DAC Switch' 1
  s 'SpkrLeft VISENSE Switch' 0
  s 'SpkrLeft PA Volume' 12
  dump idle
  if [ -d $T/events/regmap ]; then
    echo 0 > $T/tracing_on
    # boot-time trace (cmdline trace_event=regmap:regmap_reg_write): amp probe/init, codec init, mixer routing
    echo "== regmap boot trace"; grep -v 17c00000.mailbox $T/trace
    echo > $T/trace; echo 16384 > $T/buffer_size_kb
    echo 1 > $T/events/regmap/regmap_reg_write/enable; echo 1 > $T/tracing_on
  else echo "== no regmap trace events"; fi
  echo "== play 440 Hz (background)"
  speaker-test -D "plughw:$CARD,0" -c 1 -r 48000 -t sine -f 440 -l 3 > /tmp/st.log 2>&1 &
  sleep 3
  dump playing
  wait
  [ -d $T/events/regmap ] && { echo 0 > $T/tracing_on; echo "== regmap write trace"; cat $T/trace; }
  echo "== speaker-test output"; cat /tmp/st.log
  echo "== audiotest done $(cut -d' ' -f1 /proc/uptime)"
} > "$LOG" 2>&1
