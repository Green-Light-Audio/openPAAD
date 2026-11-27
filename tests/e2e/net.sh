#!/bin/sh
# Copyright 2026 AIASK Limited (Green Light Audio)
# SPDX-License-Identifier: BSD-3-Clause
#
# Two simulated devices on a real network interface of the host: a Linux
# veth pair, in real time. Device a hosts a controller that binds b's audio
# input to a's audio output, unbinds it and binds it again; everything goes
# through the kernel as real Ethernet frames.
#
#   - each device must discover the other's entity;
#   - every controller command must succeed, and both devices must open and
#     close the stream when told;
#   - gPTP must hold b within 1 ms of a over the last 5 s (software
#     timestamps on a host interface: coarse, but locked);
#   - audio must reach b after the reconnection (not every frame: in real
#     time a host loses some, decision 0013).
#
# Needs Linux, root or passwordless sudo (to create the veth pair and
# capture on it), and openpaad-sim built with libpcap; skipped otherwise
# (exit status 77).
#
# Usage: net.sh path/to/openpaad-sim [seconds (>= 20)]

set -eu

sim=$1
secs=${2:-20}
script="connect@3,disconnect@11,connect@14"

skip() { echo "SKIP: $*"; exit 77; }

[ "$(uname -s)" = Linux ] || skip "needs Linux (veth); on macOS, see docs/building.md (feth)"
command -v ip >/dev/null 2>&1 || skip "needs the ip command"
if [ "$(id -u)" -eq 0 ]; then
	sudo=""
elif sudo -n true 2>/dev/null; then
	sudo="sudo -n"
else
	skip "needs root or passwordless sudo"
fi

if_a="pd$$a"
if_b="pd$$b"
dir=$(mktemp -d "${TMPDIR:-/tmp}/openpaad-net.XXXXXX")
cleanup() {
	$sudo ip link del "$if_a" 2>/dev/null || true
	rm -rf "$dir"
}
trap cleanup EXIT

$sudo ip link add "$if_a" type veth peer name "$if_b" || skip "cannot create a veth pair"
$sudo ip link set "$if_a" up
$sudo ip link set "$if_b" up

$sudo "$sim" --net "$if_a" --side a --control "$script" -t "$secs" --log "$dir/a.log" >"$dir/a.out" 2>&1 &
pid_a=$!
$sudo "$sim" --net "$if_b" --side b -t "$secs" --log "$dir/b.log" >"$dir/b.out" 2>&1 &
pid_b=$!

status=0
wait "$pid_a" || status=1
wait "$pid_b" || status=1
if grep -q "built without libpcap" "$dir/a.out"; then
	skip "openpaad-sim built without libpcap"
fi
if [ "$status" -ne 0 ]; then
	echo "FAIL: a simulator exited with an error"
	cat "$dir/a.out" "$dir/b.out"
	exit 1
fi

fail=0
for pair in "a.log 02005afffead0002" "b.log 02005afffead0001"; do
	set -- $pair
	if ! grep -q "adp_discovery_update.*entity : $2" "$dir/$1"; then
		echo "FAIL: ${1%.log} did not discover $2"
		fail=1
	fi
done
[ "$fail" -eq 0 ] && echo "ATDECC: each device discovered the other"

if ! awk -v secs="$secs" '
	function second(f) { t = f; sub(/^t=/, "", t); sub(/s$/, "", t); return t }
	{ dev = (FILENAME ~ /a\.out$/) ? "a" : "b" }
	/controller:/ { commands++; if ($NF != "SUCCESS") { print "controller: " $0; failed_commands++ } }
	/ATDECC: stream output 0 connected/ { if (dev == "a") out_connect++ }
	/ATDECC: stream output 0 disconnected/ { if (dev == "a") out_disconnect++ }
	/ATDECC: stream input 0 connected/ { if (dev == "b") in_connect++ }
	/ATDECC: stream input 0 disconnected/ { if (dev == "b") in_disconnect++ }
	/gPTP - shared clock:/ { t = second($2); if (dev == "a") a[t] = $(NF - 1); else b[t] = $(NF - 1) }
	/audio listener:/ { t = second($2); frames[t] = $6 }
	END {
		bad = 0

		ok = commands == 4 && !failed_commands
		printf "controller: %d commands, %s\n", commands, ok ? "all succeeded" : "FAIL (expected 4 successes)"
		if (!ok) bad = 1

		ok = out_connect == 2 && out_disconnect == 1 && in_connect == 2 && in_disconnect == 1
		printf "streams: a opened its output %d times and closed it %d times, b its input %d and %d times%s\n",
			out_connect, out_disconnect, in_connect, in_disconnect, ok ? "" : ": FAIL (expected 2 and 1)"
		if (!ok) bad = 1

		worst = -1
		for (t = secs - 4; t <= secs; t++) {
			if (!(t in a) || !(t in b)) { print "FAIL: no gPTP report at t=" t "s"; exit 1 }
			d = b[t] - a[t]; if (d < 0) d = -d
			if (d > worst) worst = d
		}
		printf "gPTP: worst difference between the devices over the last 5 s: %d ns (limit 1 ms)\n", worst
		if (worst > 1000000) bad = 1

		received = 0
		for (t = secs - 2; t <= secs; t++) received += frames[t]
		printf "audio: %d frames received over the last 3 s%s\n", received, received ? "" : ": FAIL"
		if (!received) bad = 1

		exit bad
	}' "$dir/a.out" "$dir/b.out"; then
	fail=1
fi

if [ "$fail" -ne 0 ]; then
	cat "$dir/a.out" "$dir/b.out"
	exit 1
fi
echo "PASS"
