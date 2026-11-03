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
	END {
		worst = -1; bad = 0
		for (t = secs - 4; t <= secs; t++) {
			if (!(t in a) || !(t in b) || !(t in frames)) { print "FAIL: no report at t=" t "s"; exit 1 }
			d = b[t] - a[t]; if (d < 0) d = -d
			if (d > worst) worst = d
			if (frames[t] != 48000 || gaps[t] != 0) bad = 1
		}
		printf "gPTP: worst difference between the devices over the last 5 s: %d ns (limit %d ns)\n", worst, limit
		printf "audio: last 5 s %s\n", bad ? "INCOMPLETE" : "complete (48000 frames per second, no gap)"
		exit (worst > limit || bad)
	}' "$dir/a.out" "$dir/b.out"; then
	cat "$dir/a.out" "$dir/b.out"
	exit 1
fi
echo "PASS"
