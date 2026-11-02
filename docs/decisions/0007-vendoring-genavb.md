# 0007. Vendor GenAVB as a git subtree, with additive changes

- Status: accepted
- Date: 2026-11-02

## Decision

- GenAVB lives in `third_party/genavb` as a squashed git subtree, so a
  plain clone builds without submodules.
- Changes inside `third_party/genavb` are kept small, each in its own commit
  whose subject starts with `genavb:`, and listed in
  `third_party/README.md`. New behaviour goes in openPAAD's own directories
  (`paad/`, `hal/`, `boards/`) whenever GenAVB offers a hook.
- Updating upstream: `scripts/update-genavb.sh <tag>` runs
  `git subtree pull --squash`, then the full test suite.

## Consequences

The delta to upstream stays readable, which keeps both upstreaming and
rebasing possible.
