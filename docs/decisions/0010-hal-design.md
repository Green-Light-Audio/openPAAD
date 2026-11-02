# 0010. Design of the openPAAD HAL

- Status: accepted (HAL version 0.1, may change until openPAAD 1.0)
- Date: 2026-11-02
- Implements: 0009

## Decision

The HAL lives in `hal/include/paad/hal/` and has eight parts: `clock`
(gPTP hardware clock), `timer`, `eth` (Ethernet with timestamps and
shaping), `mclk` (media clock), `audio`, `storage`, `platform`, and
`common` conventions.

1. **Objects, not global functions.** Each service is a struct holding an
   operations table, private data and a few public fields. A platform can
   have several instances (two network ports, several timers), and tests can
   plug in fakes without link tricks.
2. **One time base: nanoseconds of the gPTP clock.** No cycle counts or
   fixed-point conversions cross the boundary; a port converts internally.
3. **Timestamps are corrected by the port.** PHY and MAC latencies are board
   properties, so the port applies them and the core sees medium-referenced
   times.
4. **Context rules are explicit.** Every operation is either task-only or
   documented ISR-safe; every callback may run in interrupt context.
5. **Shaping is a bandwidth, not register values.** The core gives each
   stream queue its reserved bandwidth in bit/s (computed by `paad/`); the
   port turns it into its hardware's credit parameters.
6. **The media clock is "steer and capture".** A frequency adjustment in ppb
   plus captures of an audio-clock counter at chosen gPTP instants. This
   covers a fractional audio PLL with timer capture (RT1170) and leaves room
   for other schemes behind the same calls.
7. **Audio samples are interleaved, left-justified 32-bit integers,** handed
   over one period at a time with the media clock count of the first frame.

## Integration with GenAVB

openPAAD adds one generic port driver to GenAVB's OS layer that implements
GenAVB's network port, hardware clock and hardware timer interfaces on top
of the HAL (new GenAVB driver type, a small `genavb:` change). The simulator
is the first platform to use it.

The RT1170 keeps GenAVB's native ENET driver until its own HAL port exists,
so hardware bring-up (Phase 2) does not wait for the migration.

## Consequences

A new platform implements eight small, documented interfaces and never
reads GenAVB code. Interfaces are versioned (`PAAD_HAL_VERSION_*`) and frozen
at openPAAD 1.0.
