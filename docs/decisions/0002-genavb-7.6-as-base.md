# 0002. Base the stack on NXP GenAVB/TSN 7.6.2

- Status: accepted
- Date: 2026-11-02

## Context

Writing gPTP, SRP, AVTP and ATDECC from scratch would take years. NXP's
GenAVB/TSN is BSD-3-Clause, runs on FreeRTOS on i.MX RT, and implements most
of what a pro-audio endpoint needs (Milan v1.2 level).

Release history matters for the i.MX RT1176:

| GenAVB release | Date | RT1176 AVB endpoint |
| --- | --- | --- |
| 7.3.2 | 2025-07 | supported, not validated |
| 7.5.1 | 2025-11 | supported, not validated |
| 7.6.2 | 2026-03 | **supported and validated** |
| 8.0.0 to 8.3.0 | 2026-04 to 2026-07 | **no longer supported** (TSN endpoint only) |

## Decision

Vendor GenAVB/TSN 7.6.2 (tag `7_6_2`, commit e851a19) under
`third_party/genavb` as a git subtree. openPAAD maintains this line for the
RT1176: AVB on this SoC is no longer developed upstream. Fixes useful to
NXP's current line are offered back where they apply.

## Consequences

- We start from code NXP validated on our exact SoC.
- We own maintenance of the 7.6 line from here. Protocol fixes from 8.x can be
  ported when relevant (the protocol code is shared across SoCs).
- Milan v1.3 gaps found so far are listed in `docs/requirements/README.md`.
