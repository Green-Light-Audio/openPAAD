#!/usr/bin/env bash
# Fetch openPAAD's pinned dependencies with west.
#
# Run from anywhere; the west workspace is the parent of this repository.
#   scripts/bootstrap.sh              host and firmware dependencies
#   scripts/bootstrap.sh --host-only  skip the 1.5 GB SDK examples (board files)
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workspace="$(dirname "${repo_dir}")"
repo_name="$(basename "${repo_dir}")"

command -v west >/dev/null || { echo "west not found: pip install west" >&2; exit 1; }

cd "${workspace}"
# A .west directory without a manifest path (created in advance to exclude it
# from a sync tool, or left by an interrupted run) is not a usable workspace:
# remove it so init can run. It only ever holds west's own configuration.
if [ -d .west ] && ! west config manifest.path >/dev/null 2>&1; then
  rm -rf .west
fi
if [ ! -d .west ]; then
  west init -l "${repo_name}"
fi

if [ "${1:-}" = "--host-only" ]; then
  west config manifest.group-filter -- -board-files
else
  west config manifest.group-filter -- +board-files
fi

# --narrow fetches exactly the pinned commits; a plain shallow update only
# fetches branch tips and fails when a pinned commit is no longer a tip.
west update --narrow
echo "Dependencies ready in ${workspace}/deps"
