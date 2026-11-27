#!/bin/sh
# Copyright 2026 AIASK Limited, trading as Green Light Audio
# SPDX-License-Identifier: BSD-3-Clause
#
# Two simulated devices, each a separate openpaad-sim process with the full
# stack, joined by a virtual cable:
#   - gPTP must bring them to the same time: once locked, both print the same
#     "gPTP - shared clock" value, within LIMIT ns over the last 5 seconds;
#   - each must discover the other's ATDECC entity.
#
# Usage: two_devices.sh path/to/openpaad-sim [seconds] [limit_ns]

set -eu

sim=$1
secs=${2:-25}
limit=${3:-100}

dir=$(mktemp -d "${TMPDIR:-/tmp}/openpaad-e2e.XXXXXX")
cable="e2e$$"
trap 'rm -rf "$dir" /tmp/openpaad-"$cable"-a.sock /tmp/openpaad-"$cable"-b.sock' EXIT

"$sim" --cable "$cable" --side a -t "$secs" --log "$dir/a.log" >"$dir/a.out" 2>&1 &
pid_a=$!
"$sim" --cable "$cable" --side b -t "$secs" --log "$dir/b.log" >"$dir/b.out" 2>&1 &
pid_b=$!

status=0
wait "$pid_a" || status=1
wait "$pid_b" || status=1
if [ "$status" -ne 0 ]; then
	echo "FAIL: a simulator exited with an error"
	cat "$dir/a.out" "$dir/b.out"
	exit 1
fi

# gPTP: compare the per-second values of both sides over the last 5 s.
if ! awk -v secs="$secs" -v limit="$limit" '
	/gPTP - shared clock:/ {
		t = $2; sub(/^t=/, "", t); sub(/s$/, "", t)
		v = $(NF - 1)
		if (FILENAME ~ /a\.out$/) a[t] = v; else b[t] = v
	}
	END {
		worst = -1
		for (t = secs - 4; t <= secs; t++) {
			if (!(t in a) || !(t in b)) { print "FAIL: no value at t=" t "s"; exit 1 }
			d = b[t] - a[t]; if (d < 0) d = -d
			if (d > worst) worst = d
		}
		printf "gPTP: worst difference between the devices over the last 5 s: %d ns (limit %d ns)\n", worst, limit
		exit worst > limit
	}' "$dir/a.out" "$dir/b.out"; then
	cat "$dir/a.out" "$dir/b.out"
	exit 1
fi

# ATDECC: entity IDs come from each device's MAC address (side a: ...:01,
# side b: ...:02; apps/openpaad-sim/entity.c).
id_a=02005afffead0001
id_b=02005afffead0002
if ! grep -q "adp_discovery_update.*entity : $id_b" "$dir/a.log" ||
   ! grep -q "adp_discovery_update.*entity : $id_a" "$dir/b.log"; then
	echo "FAIL: the devices did not discover each other"
	for side in a b; do
		echo "--- device $side: link and ATDECC advertising"
		grep -E "link is (UP|DOWN)|adp_discovery_update|adp_advertise_send_packet|adp_discovery_send_packet" \
			"$dir/$side.log" | head -20
	done
	exit 1
fi
echo "ATDECC: each device discovered the other"
echo "PASS"
