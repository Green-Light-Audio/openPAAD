# openPAAD

**A portable, open-source firmware stack for professional audio over AVB,
designed to interoperate with Milan devices. Reference platform: NXP
i.MX RT1170.**

openPAAD turns a microcontroller or embedded processor into a pro-audio AVB
endpoint: gPTP time, stream reservation, AAF audio and CRF media clock
streams, and the ATDECC entity model that controllers such as Hive use to
discover and connect devices. It builds on NXP's BSD-licensed GenAVB/TSN
stack, adds what Milan v1.3 requires on top, and builds entirely from public
sources.

The stack has three layers. A portable core (protocols and the pro-audio
layer) contains no chip-specific code and is built and tested on Linux and
macOS on every commit. An explicit hardware abstraction layer (HAL) defines
what a platform must provide: hardware timestamps, traffic shaping, clocks,
media clock control and audio I/O. Each platform is a port of that HAL. The
NXP i.MX RT1170 is the first and reference port; the host simulator is the
second.

> Status: **Phase 1, HAL and simulator.** The full stack runs on a PC or Mac
> on simulated hardware, where gPTP synchronises two ports; the firmware
> links for the MIMXRT1170-EVKB but has not run on hardware yet. Nothing here
> is ready for a stage. See [the roadmap](docs/roadmap.md).

## Why

Pro-audio AVB devices today run closed, per-unit-licensed stacks. openPAAD
aims to be the stack a hardware designer can read, test, fix and ship,
with:

- a reference board anyone can buy (NXP MIMXRT1170-EVKB, about USD 183);
- a requirement matrix that maps every clause of the interoperability
  specification to a test (`docs/requirements/`);
- a host simulator that runs the real protocol code in CI;
- hardware-in-the-loop tests on real boards and interop sessions with
  certified equipment.

## What works today

| Area | State |
| --- | --- |
| GenAVB/TSN 7.6.2 for the RT1176 Cortex-M7, from public sources | builds, `-Werror`, no warnings |
| Reference endpoint image (boot header, FreeRTOS, full AVB stack init) | links; not yet run on a board |
| Reproducible firmware | same commit, same `.bin`, on any machine |
| All GenAVB protocol code built for the host | builds, `-Wall -Werror` |
| Full GenAVB stack on simulated hardware (`openpaad-sim`, Linux and macOS) | runs; two simulated devices, one process each, lock gPTP to within a few nanoseconds and discover each other with ATDECC, checked in CI |
| Stream Tspec and bandwidth per Milan v1.3 Table 4.4 | implemented and tested (`paad/`) |
| Requirement matrix v0 | 100 rows, 7 confirmed gaps in GenAVB 7.6.2 |

## Platforms

| Platform | Role | State |
| --- | --- | --- |
| NXP i.MX RT1170 (MIMXRT1170-EVKB) | Reference port | Builds and links; hardware bring-up next |
| Host simulator (Linux, macOS) | Development and CI | Full stack runs; simulated devices cabled across processes; audio and real-network back-ends next |
| Further ports | Community and partners | After 1.0; see [hal/README.md](hal/README.md) |

## Quick start

Requirements: Linux or macOS, Git, CMake 3.22 or later, Ninja, Python 3,
[west](https://docs.zephyrproject.org/latest/develop/west/index.html), and
the xPack Arm GCC 14.2.1-1.1 toolchain (see
[docs/building.md](docs/building.md)), or simply open the repository in the
devcontainer.

```sh
mkdir openpaad-workspace && cd openpaad-workspace
git clone <this repository> openpaad
openpaad/scripts/bootstrap.sh          # fetches pinned dependencies with west
cd openpaad
cmake --preset host && cmake --build --preset host && ctest --preset host
cmake --preset evkb-rt1170 && cmake --build --preset evkb-rt1170
```

The firmware image is `build/evkb-rt1170/apps/ref-endpoint/openpaad-ref-endpoint.{elf,bin,hex}`.

## Layout

```
apps/ref-endpoint/   reference endpoint application
boards/evkb-rt1170/  board configuration, start-up, linker script
paad/                openPAAD's pro-audio layer on top of GenAVB (portable)
paad/genavb/         GenAVB network driver on top of the HAL
apps/openpaad-sim/   the stack on simulated hardware (host builds)
hal/                 hardware abstraction layer and its ports (rt1170, sim)
third_party/genavb/  NXP GenAVB/TSN 7.6.2 (git subtree)
tests/               unit, fuzz, conformance and hardware-in-the-loop tests
docs/                decisions, requirement matrix, hardware notes, timing targets
west.yml             pinned external dependencies
```

## Licence and names

openPAAD is BSD-3-Clause. Third-party code keeps its own licence (see
`third_party/README.md`).

Milan is a trademark of Avnu Alliance. openPAAD is an independent project,
not affiliated with or certified by Avnu Alliance. Products built on it need
their own certification to use the Milan name. The repository contains no
text from the IEEE or Avnu specifications; see
[decision 0003](docs/decisions/0003-licence-naming-specs.md).

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md). In short: sign off your commits
(DCO), keep GenAVB changes small and separate, and never paste specification
text.

openPAAD was started by Green Light Audio.
