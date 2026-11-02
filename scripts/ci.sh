#!/usr/bin/env bash
# The CI pipeline, runnable locally. Same steps as .github/workflows/ci.yml.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

echo "== host: build and unit tests"
cmake --preset host
cmake --build --preset host
ctest --preset host

echo "== firmware: MIMXRT1170-EVKB"
if [ -z "${ARMGCC_DIR:-}" ] && ! command -v arm-none-eabi-gcc >/dev/null; then
  echo "ARMGCC_DIR not set and arm-none-eabi-gcc not on PATH" >&2
  exit 1
fi
cmake --preset evkb-rt1170
cmake --build --preset evkb-rt1170

echo "== checks"
scripts/check-no-spec-text.sh

echo "CI passed"
