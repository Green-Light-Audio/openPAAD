# Requirement matrix

`matrix.csv` lists what a pro-audio AVB endpoint must do to interoperate with
Milan-certified equipment, clause by clause, and where openPAAD stands on
each point.

**Version 0 (Phase 0).** 100 rows at sub-clause level, covering Milan
Specification v1.3 chapters 4 to 8. Status of the vendored GenAVB/TSN 7.6.2
for the rows that apply to the reference endpoint:

| Status | Rows | Meaning |
| --- | --- | --- |
| implemented | 45 | Present upstream; still to be verified by a test |
| unknown | 32 | Not yet checked against the code |
| partial | 15 | Present but incomplete or v1.2 behaviour |
| gap | 7 | Missing, or contradicts v1.3 |

Phase 5 drives every applicable row to a passing test.

## Rules for this file

- **Paraphrase, never quote.** The Milan and IEEE specifications are
  copyrighted. Each row states the requirement in our own words and points to
  the clause number. Contributors need their own copy of the specifications
  to check the details.
- **One row, one testable behaviour.** Split rows when a test would need to
  check two unrelated things.
- **Status comes from evidence**: a test, a code reading noted in `notes`, or
  an interop session log. `unknown` is honest; guessing is not.
- `verification` names the cheapest level that can prove the row:
  `unit` (host tests), `sim` (host network simulation), `conformance`
  (`tests/conformance`: a controller built on la_avdecc, the library Hive
  uses, against simulated devices), `fuzz`, `hil` (hardware-in-the-loop on
  the lab runner) or `interop` (third-party equipment).

## Known gaps in GenAVB 7.6.2 (found in Phase 0)

| Row | Finding |
| --- | --- |
| BW-001 | Talker Tspec MaxFrameSize includes the one-byte drift allowance (Milan v1.2 rule). v1.3 moved that byte into the bandwidth computation. Pinned by `GenavbCharacterisation.AafTspecIsMilan12Style`. |
| BW-002 | The shaper idle slope adds the drift byte a second time: 64 kbit/s above the published bandwidth per stream. Harmless, but documented. |
| BW-003 | CRF bandwidth changes from 5632 to 5696 kbit/s in v1.3 because of the order of the minimum-frame and drift rules. |
| CTL-019 | SET/GET_MAX_TRANSIT_TIME not implemented. |
| CTL-030 | GET_MILAN_INFO reports certification version 1.1.0.0. An uncertified implementation must report 0. |
| CTL-033 to 035 | MVU BIND_STREAM, UNBIND_STREAM and GET_STREAM_INPUT_INFO_EX (new in v1.3) not implemented. |

## Found by the conformance harness (Phase 1)

| Row | Finding |
| --- | --- |
| ADP-001 | GenAVB's RTOS defaults advertise a valid time of 31 (62 s); Milan's value has to be set by the application (GenAVB's Linux build forces it). openpaad-sim sets it. |
| ADP-001, CTL-011 | The reference entity model has no IDENTIFY control: the identify control index is not advertised and identifying fails. openPAAD's entity model must add one. |
| CTL-008 | GET_NAME on the MEMORY_OBJECT descriptor answers BAD_ARGUMENTS. |
| CTL-017 | GenAVB leaves the audio map commands to the application and answers ENTITY_MISBEHAVING when none listens; la_avdecc (and Hive) then flag the whole entity as misbehaving. openpaad-sim now answers them. |
| CTL-018 | Inside GET_DYNAMIC_INFO, sub-commands GenAVB does not implement are answered NOT_SUPPORTED with no data; la_avdecc rejects the whole answer, so its fast enumeration fails. |
