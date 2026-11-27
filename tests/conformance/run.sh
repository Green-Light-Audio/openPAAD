#!/bin/sh
# Copyright 2026 AIASK Limited, trading as Green Light Audio
# SPDX-License-Identifier: BSD-3-Clause
#
# ATDECC conformance run: two simulated devices share one end of a Linux veth
# pair, as on one network segment; the la_avdecc controller (conformance.cpp)
# sits on the other end, discovers them, reads their entity models and puts
# them through commands, binding b's audio input to a's audio output. Each
# check is reported against docs/requirements/matrix.csv; the run fails if a
# check of a row marked "implemented" there fails.
#
# Needs Linux, root or passwordless sudo (to create the veth pair and
# capture on it), and openpaad-sim built with libpcap; skipped otherwise
# (exit status 77).
#
# Usage: run.sh path/to/openpaad-sim path/to/openpaad-conformance path/to/matrix.csv

set -eu

sim=$1
harness=$2
matrix=$3
secs=60

skip() { echo "SKIP: $*"; exit 77; }

[ "$(uname -s)" = Linux ] || skip "needs Linux (veth)"
command -v ip >/dev/null 2>&1 || skip "needs the ip command"
if [ "$(id -u)" -eq 0 ]; then
	sudo=""
elif sudo -n true 2>/dev/null; then
	sudo="sudo -n"
else
	skip "needs root or passwordless sudo"
fi

if_dev="pc$$d"
if_ctl="pc$$c"
dir=$(mktemp -d "${TMPDIR:-/tmp}/openpaad-conformance.XXXXXX")
pids=""
cleanup() {
	for pid in $pids; do
		$sudo kill "$pid" 2>/dev/null || true
	done
	$sudo ip link del "$if_dev" 2>/dev/null || true
	rm -rf "$dir"
}
trap cleanup EXIT

$sudo ip link add "$if_dev" type veth peer name "$if_ctl" || skip "cannot create a veth pair"
$sudo ip link set "$if_dev" up
$sudo ip link set "$if_ctl" up

for side in a b; do
	$sudo "$sim" --net "$if_dev" --side "$side" -t "$secs" --log "$dir/$side.log" >"$dir/$side.out" 2>&1 &
	pids="$pids $!"
done

sleep 1
if grep -q "built without libpcap" "$dir/a.out"; then
	skip "openpaad-sim built without libpcap"
fi

status=0
$sudo "$harness" --interface "$if_ctl" --talker 02005afffead0001 --listener 02005afffead0002 --matrix "$matrix" || status=$?

if [ "$status" -ne 0 ]; then
	echo "--- device a"
	tail -n 20 "$dir/a.out"
	echo "--- device b"
	tail -n 20 "$dir/b.out"
fi

exit "$status"
