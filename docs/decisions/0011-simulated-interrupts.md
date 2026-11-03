# 0011. Simulated hardware runs inside FreeRTOS

- Status: accepted
- Date: 2026-11-02
- Refines: 0005

## Context

On the host, FreeRTOS tasks are threads of the POSIX port: the kernel lets
one run at a time and switches them with signals. GenAVB expects hardware
events (timer expiries, frames, transmit completions) to arrive as
interrupts, whose handlers use the kernel's `*FromISR()` services. Calling
those services from a thread the kernel does not know corrupts its state,
and a task suspended while holding one of the simulator's mutexes blocks
every task that needs it.

## Decision

1. The simulation world runs in a FreeRTOS task of the highest priority
   (`paad_sim_freertos_start()`), which plays the role of the interrupt
   controller: it sleeps until the next simulated event or until woken,
   then runs every event due. Callbacks therefore run in kernel context and
   preempt every other task, as interrupts do.
2. The simulator's locked sections are bracketed by hooks that disable
   preemption (`taskENTER_CRITICAL()`), so no task is ever suspended inside
   one.
3. Resolution is the FreeRTOS tick (1 ms). Timestamps stay exact, because
   the simulator computes them from simulated time, not from when a task
   happens to run.
4. The idle task sleeps until the next tick instead of spinning.
5. The C library's own locks get the same treatment: console output and the
   simulator's allocations run with preemption disabled
   (`hal/sim/src/console.c`, `paad_sim_printf()`), since a task suspended
   inside `printf()` or `malloc()` would block the next caller for good.
6. On a busy host FreeRTOS loses ticks; anything that must follow real time
   is paced by the simulated clock, not by tick counts.

## Consequences

The real stack runs unchanged and deterministically enough for CI: gPTP
locks two simulated ports to within a few nanoseconds. Event latency is up
to one tick, which is fine for protocol work; audio streaming at 8 kHz
packet rates may need a faster tick (configurable) when it arrives.
