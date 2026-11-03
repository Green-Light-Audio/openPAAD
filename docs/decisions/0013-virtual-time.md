# 0013. The simulator runs in virtual time

- Status: accepted
- Date: 2026-11-03
- Refines: 0011, 0012

## Context

GenAVB's network and media clock code runs on a 125 µs period and expects
it on time: its gPTP-derived media clock emits at most one timestamp per
period. FreeRTOS on a host cannot deliver that. With a 1 ms tick the
periods arrive in bursts of eight and most timestamps are lost; with a
125 µs tick the host cannot keep up. Audio streams between simulated
devices lost a third of their frames or more.

## Decision

1. Virtual time (`--virtual`, world in `PAAD_SIM_VIRTUAL_TIME`): time stands
   still while any FreeRTOS task has work. When all are idle, the idle task
   moves it to the next simulated event (timer expiry, frame arrival) or
   FreeRTOS tick, whichever comes first, and triggers it. Events run in the
   simulated interrupt task (decision 0011), ticks come from the POSIX
   port's own tick thread, which then waits on a gate instead of sleeping
   (`usleep` renamed when building `port.c`; the port is otherwise
   unchanged).
2. Devices cabled across processes (decision 0012) advance in lockstep:
   each announces its next event over the cable, and a process never runs
   past the peer's next event, or the arrival of a frame the peer has not
   acknowledged, plus the cable's propagation delay. A goodbye, or 10 s
   of silence, releases the other side.
3. Real time stays: it is the only way to talk to real controllers and
   equipment, and fine for protocol work that does not stream audio.

## Consequences

Every timer fires on time, whatever the host's load or speed: two devices
stream audio without losing a frame, and gPTP holds them within 2 ns.
Tests become reproducible. Two devices in lockstep run about 4 times slower
than real time (one process about as fast), since with a cable delay of
500 ns each must wait for the other at almost every step.
