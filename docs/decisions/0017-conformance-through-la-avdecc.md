# 0017. ATDECC conformance checked through la_avdecc

- Status: accepted
- Date: 2026-11-03
- Refines: 0016

## Context

Phase 1 must show that simulated devices are discovered and connected by
the controllers openPAAD has to work with, Hive first, on every commit.
Hive is a desktop application: it cannot run in CI, and on macOS it lists
only the machine's Ethernet ports, so it cannot reach simulated devices
on a virtual interface pair (decision 0016 assumed it could). Hive is built
on la_avdecc, L-Acoustics' open-source ATDECC library, which also checks
what it reads from an entity and rates it (IEEE 1722.1, Milan, or
misbehaving).

## Decision

1. The conformance harness (`tests/conformance`) is a controller built on
   la_avdecc. It runs against two simulated devices on a Linux veth pair:
   discovery, entity model, lock, names, identify, clock source, unsolicited
   notifications (to a second controller), dynamic audio mappings, binding
   and unbinding, and la_avdecc's own rating of each entity.
2. Every check names the rows of `docs/requirements/matrix.csv` it covers.
   It fails the run only if all its rows are marked `implemented`; the
   others report, and say when a row may be updated. The matrix and the
   harness so keep each other honest.
3. la_avdecc is LGPL. It is a test-only dependency, fetched at a pinned
   release by the `conformance` preset, and never linked into firmware or
   into the simulator. Normal builds do not fetch it.
4. The harness enumerates entities as la_avdecc does by default, as Hive
   does, so that what passes here is what Hive sees.
5. CI runs it on every commit (Linux, passwordless sudo). Elsewhere it is
   skipped.

## Consequences

The first run found what Hive would have shown: the entities were rated
misbehaving (GenAVB leaves the audio map commands to the application and
answers ENTITY_MISBEHAVING when none listens), advertised the wrong valid
time, and had no IDENTIFY control. openpaad-sim now answers the audio map
commands and sets Milan's valid time; la_avdecc rates the devices IEEE
1722.1 and Milan 1.2 compatible. The other findings are in the matrix.

Trying Hive itself needs an Ethernet port with a link on the machine
running it (`docs/building.md`). Real Milan equipment is the interop
stage (Phase 4).
