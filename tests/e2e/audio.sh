#!/bin/sh
# Copyright 2026 AIASK Limited (Green Light Audio)
# SPDX-License-Identifier: BSD-3-Clause
#
# Two simulated devices connected by a controller, in virtual time (decision
# 0013). Device a hosts the controller, which, as soon as it has discovered
# both entities, sets b's clock source to its audio input and binds b's
# audio input to a's audio output at t=1 s, unbinds it at 9 s and binds it
# again at 12 s. The stream is AAF (2 ch, 32 bit, 48 kHz, class A), reserved
# by GenAVB's ATDECC and SRP.
#
#   - every controller command must succeed;
#   - from t=6 to 8 s, b must receive all 48000 frames of every second, in
#     order;
#   - at t=10 and 11 s, neither device may have a stream left;
#   - over the last 5 s, after reconnection: every frame again; the devices
#     agree on gPTP time within LIMIT ns; a's audio clock locked to gPTP and
#     b's to the stream, both at gPTP rate within 0.1 ppm, with no restart of
#     the recovery, their phase within 150 ns (under 4 ticks of 24.576 MHz)
#     of a perfect gPTP-rate clock since lock.
#
# Usage: audio.sh path/to/openpaad-sim [seconds (>= 20)] [limit_ns]

set -eu

sim=$1
secs=${2:-20}
limit=${3:-100}
script="connect@1,disconnect@9,connect@12"

dir=$(mktemp -d "${TMPDIR:-/tmp}/openpaad-audio.XXXXXX")
cable="au$$"
trap 'rm -rf "$dir" /tmp/openpaad-"$cable"-a.sock /tmp/openpaad-"$cable"-b.sock' EXIT

"$sim" --virtual --cable "$cable" --side a --control "$script" -t "$secs" --log "$dir/a.log" >"$dir/a.out" 2>&1 &
pid_a=$!
"$sim" --virtual --cable "$cable" --side b -t "$secs" --log "$dir/b.log" >"$dir/b.out" 2>&1 &
pid_b=$!

status=0
wait "$pid_a" || status=1
wait "$pid_b" || status=1
if [ "$status" -ne 0 ]; then
	echo "FAIL: a simulator exited with an error"
	cat "$dir/a.out" "$dir/b.out"
	exit 1
fi

if ! awk -v secs="$secs" -v limit="$limit" '
	function second(f) { t = f; sub(/^t=/, "", t); sub(/s$/, "", t); return t }
	{ dev = (FILENAME ~ /a\.out$/) ? "a" : "b" }
	/controller:/ {
		commands++
		if ($NF != "SUCCESS") { print "controller: " $0; failed_commands++ }
	}
	/gPTP - shared clock:/ {
		t = second($2)
		if (dev == "a") a[t] = $(NF - 1); else b[t] = $(NF - 1)
	}
	/audio listener:/ { t = second($2); frames[t] = $6; gaps[t] = $8 }
	/audio: no stream connected/ { t = second($2); none[dev, t] = 1 }
	# t=Ns media clock: STATE to SOURCE, adjust X ppm, error X ppm, phase X ns, max X ns, resets N
	/media clock:/ {
		t = second($2)
		src = $7; sub(/,$/, "", src)
		mc_state[dev, t] = $5 " to " src; mc_err[dev, t] = $12; mc_max[dev, t] = $18; mc_resets[dev, t] = $21
	}
	function complete(t) { return (t in frames) && frames[t] == 48000 && gaps[t] == 0 }
	END {
		bad = 0

		ok = commands == 4 && !failed_commands
		printf "controller: %d commands, %s\n", commands, ok ? "all succeeded" : "FAIL (expected 4 successes)"
		if (!ok) bad = 1

		ok = 1
		for (t = 6; t <= 8; t++) if (!complete(t)) ok = 0
		printf "first connection: %s\n", ok ? "complete audio from t=6 to 8 s" : "INCOMPLETE audio from t=6 to 8 s"
		if (!ok) bad = 1

		ok = 1
		for (t = 10; t <= 11; t++) if (!(("a", t) in none) || !(("b", t) in none)) ok = 0
		printf "disconnection: %s\n", ok ? "no stream left at t=10 and 11 s" : "FAIL: a stream was still connected"
		if (!ok) bad = 1

		worst = -1; audio_bad = 0; mc_bad = 0; err_worst = 0; phase_worst = 0
		want["a"] = "locked to gPTP"; want["b"] = "locked to stream"
		for (t = secs - 4; t <= secs; t++) {
			if (!(t in a) || !(t in b)) { print "FAIL: no gPTP report at t=" t "s"; exit 1 }
			d = b[t] - a[t]; if (d < 0) d = -d
			if (d > worst) worst = d
			if (!complete(t)) audio_bad = 1
			for (dev in want) {
				if (!((dev, t) in mc_state)) { print "FAIL: no media clock report from " dev " at t=" t "s"; exit 1 }
				if (mc_state[dev, t] != want[dev]) {
					printf "media clock %s at t=%ds: %s (expected %s)\n", dev, t, mc_state[dev, t], want[dev]
					mc_bad = 1
				}
				e = mc_err[dev, t] + 0; if (e < 0) e = -e
				if (e > err_worst) err_worst = e
				if (mc_max[dev, t] + 0 > phase_worst) phase_worst = mc_max[dev, t] + 0
				if (mc_resets[dev, t] != mc_resets[dev, secs - 4]) mc_bad = 1
			}
		}
		if (err_worst > 0.1 || phase_worst > 150) mc_bad = 1
		printf "reconnection, last 5 s: audio %s\n", audio_bad ? "INCOMPLETE" : "complete (48000 frames per second, no gap)"
		printf "gPTP: worst difference between the devices over the last 5 s: %d ns (limit %d ns)\n", worst, limit
		printf "media clock: a to gPTP, b to the stream: worst rate error %.3f ppm (limit 0.1), phase %d ns (limit 150)%s\n",
			err_worst, phase_worst, mc_bad ? ": FAIL" : ""
		exit (bad || audio_bad || worst > limit || mc_bad)
	}' "$dir/a.out" "$dir/b.out"; then
	cat "$dir/a.out" "$dir/b.out"
	exit 1
fi
echo "PASS"
