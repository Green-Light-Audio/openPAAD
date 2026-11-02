#!/usr/bin/env bash
# Guard against pasted specification text (decision 0003).
#
# Flags long sentences in openPAAD's own files that use the normative
# vocabulary of standards documents. Not a proof of anything, a tripwire:
# a hit means "a human should look", and the line can be reworded.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

paths=(apps boards paad hal tests docs README.md CONTRIBUTING.md)
pattern='\b(shall|shall not)\b.{60,}'

if grep -rniE "${pattern}" "${paths[@]}" --include='*.c' --include='*.h' --include='*.cpp' \
     --include='*.md' --include='*.csv' --exclude-dir=build; then
  echo "Possible specification text found (see above). Paraphrase it." >&2
  exit 1
fi
echo "no specification-style text found"
