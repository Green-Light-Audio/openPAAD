# 0005. Simulate the stack through its RTOS layer on the host

- Status: accepted (spike result)
- Date: 2026-11-02

## Context

Phases 0 and 1 must make progress without hardware, and CI must test
protocol behaviour on every commit. Two ways to run GenAVB on a PC were
considered:

1. **GenAVB's Linux build.** Its gPTP/SRP process uses standard sockets, but
   the AVTP/AVDECC/MAAP process needs NXP's GPL kernel module (`/dev/avb`).
   Not usable as a pure user-space simulator.
2. **GenAVB's RTOS build on the FreeRTOS POSIX port**, replacing only the
   hardware drivers.

## Spike (time-boxed, 2026-11-02)

Every GenAVB 7.6.2 source file was compiled for x86-64 Linux with GCC 13,
against the FreeRTOS POSIX port headers and a host `genavb_sdk.h`:

| Area | Files | Compile unchanged |
| --- | --- | --- |
| Protocol code (common, gPTP, SRP, MAAP, AVTP, AVDECC, management, public) | 60 | 60 |
| OS layer (rtos/), API (api/rtos, api/), RTOS abstraction layer | 103 | 98 |

The five that do not are the ENET driver files (`net_port_enet*.c`,
`gptp_dev.c`, which need the NXP ENET driver) and `net_tx.c`, which uses an
Arm `clz` instruction in inline assembly.

## Decision

Build the simulator on option 2. `hal/sim` will provide, under the same
function names, the pieces the hardware build gets from the ENET and GPT
drivers: a network port on a raw socket or in-process virtual wire, a
hardware clock and timers on `CLOCK_MONOTONIC`, and a simulated media clock.
GenAVB's own code stays untouched, apart from a one-line portable fallback
for `clz` in `net_tx.c`.

The protocol code already builds for the host in CI
(`genavb_protocols_host`, `-Wall -Werror`), and the unit tests link against
it.

## Consequences

- The simulator runs the same protocol and OS-layer code as the firmware.
- Simulated timing is not real-time; timing-sensitive behaviour is still
  measured on hardware (Phase 2 and 3 gates).
