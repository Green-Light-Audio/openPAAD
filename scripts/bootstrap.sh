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
if [ ! -d .west ]; then
  west init -l "${repo_name}"
fi

if [ "${1:-}" = "--host-only" ]; then
  west config manifest.group-filter -- -board-files
else
  west config manifest.group-filter -- +board-files
fi

west update
echo "Dependencies ready in ${workspace}/deps"
