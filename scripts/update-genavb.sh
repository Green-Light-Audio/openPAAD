#!/usr/bin/env bash
# Update the vendored GenAVB/TSN subtree to an upstream tag, then run CI.
#   scripts/update-genavb.sh 7_6_3
set -euo pipefail

tag="${1:?usage: $0 <upstream tag>}"
cd "$(dirname "${BASH_SOURCE[0]}")/.."

git subtree pull --prefix third_party/genavb https://github.com/NXP/GenAVB_TSN "${tag}" --squash \
  -m "Update GenAVB/TSN to ${tag}"
scripts/ci.sh
echo "Remember to update third_party/README.md and docs/decisions if behaviour changed."
