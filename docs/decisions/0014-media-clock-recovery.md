# 0014. Media clock recovery reuses GenAVB's loop on the HAL

- Status: accepted
- Date: 2026-11-03
- Implements: 0010 (media clock: steer and capture)

## Context

A listener's audio clock has to run at its talker's rate, or samples slip.
GenAVB recovers media clocks with a control loop that its operating systems
share (`common/os/media_clock_rec_pll_common.c`): it compares how many
audio clock ticks pass between gPTP instants with how many there should be,
and steers the audio PLL. On its RTOS layer, the sampling and the PLL are
i.MX hardware: a 1588 timer compare output wired to a timer capture input,
and the i.MX audio PLL driver. The HAL's media clock (decision 0010) offers
the same two things in portable form: a frequency adjustment in ppb, and a
capture of an audio clock counter at a gPTP instant.

## Decision

1. One GenAVB media clock recovery device for every HAL platform
   (`paad/genavb/mclk_rec.c`). The first HAL network port registers it when
   the platform has a media clock. It includes GenAVB's shared loop
   unchanged and gives it the HAL: the PLL adjustment is
   `paad_hal_mclk_adjust`, the sampling at gPTP instants is
   `paad_hal_mclk_request_capture`. GenAVB itself is not modified.
2. GenAVB keeps two sampling instants programmed ahead; the HAL takes one
   capture request at a time and has no cancel. The device queues the
   instants and requests the next as soon as a capture has been used. The
   capture callback, which may run in interrupt context, only fills a
   one-slot mailbox; everything else runs in GenAVB's media clock task.
3. Like GenAVB's own drivers, the device serves clock domain 0 and follows
   the gPTP clock of port 0. GenAVB decides what it locks to: gPTP when the
   domain's source is the PTP clock (a talker), a stream's timestamps when
   it is an input stream with media clock recovery (a listener).
4. The simulator models the audio clock as an oscillator with its own
   natural error (many pro-audio designs have a separate audio oscillator,
   and an error different from the network crystal's exercises the loop),
   counted exactly with 128-bit arithmetic. Captures are exact, and the
   model is an independent reference: `openpaad-sim` measures the recovered
   clock against gPTP time, not against GenAVB's own figures.

## Consequences

In virtual time, the talker's audio clock locks to gPTP and the listener's
to the stream within about 2 s, each correcting its own oscillator (−40 ppm
and +30 ppm in the demo). Over the last seconds of the end-to-end test, both
run at gPTP rate within a few ppb, and their phase stays within about 50 ns
of a perfect clock since lock: the loop works on whole ticks of a
24.576 MHz counter (40.7 ns).

The RT1170 port will implement the same two calls with its 1588 timer
compare, a GPT capture input and the audio PLL; GenAVB's i.MX drivers keep
working meanwhile.

In real time the talker still locks to gPTP. The listener cannot, because
the stream itself loses data on a host (decision 0013).
