#!/bin/sh
# Copyright 2026 openPAAD contributors
# SPDX-License-Identifier: BSD-3-Clause
#
# Audio between two simulated devices in virtual time (decision 0013): device
# a streams AAF (2 ch, 32 bit, 48 kHz, class A, reserved with SRP) to device
# b. Over the last 5 simulated seconds, b must receive all 48000 frames of
# every second, in order, and the devices must agree on gPTP time within
# LIMIT ns.
#
# Media clock recovery, over the same 5 s: a's audio clock must be locked to
# gPTP and b's to the stream, both running at gPTP rate within 0.1 ppm, with
# no restart of the recovery, and their phase must have stayed within 150 ns
# (under 4 ticks of 24.576 MHz) of a perfect gPTP-rate clock since lock.
#
# Usage: audio.sh path/to/openpaad-sim [seconds] [limit_ns]

set -eu

sim=$1
secs=${2:-10}
limit=${3:-100}

dir=$(mktemp -d "${TMPDIR:-/tmp}/openpaad-audio.XXXXXX")
cable="au$$"
trap 'rm -rf "$dir" /tmp/openpaad-"$cable"-a.sock /tmp/openpaad-"$cable"-b.sock' EXIT

"$sim" --virtual --audio --cable "$cable" --side a -t "$secs" --log "$dir/a.log" >"$dir/a.out" 2>&1 &
pid_a=$!
"$sim" --virtual --audio --cable "$cable" --side b -t "$secs" --log "$dir/b.log" >"$dir/b.out" 2>&1 &
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
	/gPTP - shared clock:/ {
		t = second($2)
		if (FILENAME ~ /a\.out$/) a[t] = $(NF - 1); else b[t] = $(NF - 1)
	}
	/audio listener:/ {
		t = second($2); frames[t] = $6; gaps[t] = $8
	}
	# t=Ns media clock: STATE to SOURCE, adjust X ppm, error X ppm, phase X ns, max X ns, resets N
	/media clock:/ {
		t = second($2); d = (FILENAME ~ /a\.out$/) ? "a" : "b"
		src = $7; sub(/,$/, "", src)
		mc_state[d, t] = $5 " to " src; mc_err[d, t] = $12; mc_max[d, t] = $18; mc_resets[d, t] = $21
	}
	END {
		worst = -1; bad = 0; mc_bad = 0; err_worst = 0; phase_worst = 0
		want["a"] = "locked to gPTP"; want["b"] = "locked to stream"
		for (t = secs - 4; t <= secs; t++) {
			if (!(t in a) || !(t in b) || !(t in frames)) { print "FAIL: no report at t=" t "s"; exit 1 }
			d = b[t] - a[t]; if (d < 0) d = -d
			if (d > worst) worst = d
			if (frames[t] != 48000 || gaps[t] != 0) bad = 1
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
		printf "gPTP: worst difference between the devices over the last 5 s: %d ns (limit %d ns)\n", worst, limit
		printf "audio: last 5 s %s\n", bad ? "INCOMPLETE" : "complete (48000 frames per second, no gap)"
		printf "media clock: a to gPTP, b to the stream: worst rate error %.3f ppm (limit 0.1), phase %d ns (limit 150)%s\n",
			err_worst, phase_worst, mc_bad ? ": FAIL" : ""
		exit (worst > limit || bad || mc_bad)
	}' "$dir/a.out" "$dir/b.out"; then
	cat "$dir/a.out" "$dir/b.out"
	exit 1
fi
echo "PASS"
