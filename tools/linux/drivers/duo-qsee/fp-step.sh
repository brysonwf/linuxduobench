#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# One step of a fingerprint enrollment / match on the Surface Duo through duo_qsee (debugfs) - for a PC-side loop
# that speaks the progress (docs/research/workbench/secure-world.md). Needs duo_qsee loaded with app=fpctzrel,
# the sensor out of reset (GPIO 95 high) and the TA's sensor initialised.
# usage: fp-step.sh begin | touch | end | identify      prints one line: RESULT <status> <value>
Q="python3 $(dirname "$0")/qsee-cmd.py"
LOG=/root/qsee/fp.log
w() { r=$($Q "$@" --show 5 | tail -1); echo "$(date +%T.%N | cut -c1-12) $* -> $r" >> $LOG; echo "$r"; }
word() { echo "$1" | cut -d' ' -f"$2"; }
irq() { gpioget -c gpiochip3 118 | cut -d= -f2; }
capture() {   # arm, wait for a finger (15 s), capture (3 = retry)
	w 0x0a 3 > /dev/null
	i=0; while [ "$(irq)" != active ] && [ $i -lt 300 ]; do sleep 0.05; i=$((i + 1)); done
	[ "$(irq)" = active ] || { echo "RESULT nofinger 0"; echo "SAY Touch the pad."; exit 0; }
	for t in 1 2 3 4; do r=$(w 0x0a 4); [ "$(word "$r" 3)" = 00000003 ] || break; done
	[ "$(word "$r" 3)" = 00000000 ] || { echo "RESULT capturefail $(word "$r" 3)"; exit 0; }
}
lift() { i=0; while [ $i -lt 40 ]; do [ "$(word "$(w 0x0a 1)" 3)" = 00000001 ] && return; sleep 0.15; i=$((i + 1)); done; }
case "$1" in
begin)	w 0x0a 0 > /dev/null; w 0x0b 6 > /dev/null; w 0x0b 9 0 0 > /dev/null   # sensor init, empty DB, group 0
	r=$(w 0x0b 0); echo "RESULT $(word "$r" 3) 0" ;;
touch)	capture; r=$(w 0x0b 1); s=$(word "$r" 3); n=$(printf %d 0x$(word "$r" 4)); lift
	echo "RESULT $s $n"
	case $s in
	00000000) e=$(w 0x0b 2); echo "SAY Enrolled. Finger I D $(printf %d 0x$(word "$e" 4))."; echo "DONE end-status $(word "$e" 3)" ;;  # complete -> end at once
	00000001|00000005) echo "SAY $n to go." ;;
	00000006) echo "SAY Same spot, move your finger." ;;
	*) echo "SAY Error $s." ;;
	esac ;;
end)	r=$(w 0x0b 2); l=$(w 0x0b 7 0 5); echo "RESULT $(word "$r" 3) $(word "$r" 4) list $(word "$l" 4)" ;;
identify) capture; r=$(w 0x0b 3); lift; echo "RESULT $(word "$r" 3) $(word "$r" 4)"
	case $(word "$r" 3) in 00000000) echo "SAY Match. Finger $(printf %d 0x$(word "$r" 4))." ;; *) echo "SAY No match, status $(word "$r" 3)." ;; esac ;;
*)	echo "usage: $0 begin|touch|end|identify"; exit 1 ;;
esac
