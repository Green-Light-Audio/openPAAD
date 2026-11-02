# 0009. Portable core, explicit HAL, RT1170 as reference port

- Status: accepted
- Date: 2026-11-02

## Context

The goal is a generic Milan-compatible stack, not firmware for one chip.
The parts that decide whether an AVB endpoint works (hardware timestamps,
transmit shaping, media clock control) are chip-specific by nature, so no
stack can be hardware-free. GenAVB's protocol code is portable (decision
0005 compiled all of it for x86 unchanged), but its hardware boundary is its
own internal OS layer, which only NXP drivers implement.

## Decision

1. Three layers: a **portable core** (GenAVB protocols and `paad/`), an
   **openPAAD HAL** (`hal/`, specified as C headers in Phase 1), and **ports**
   (`hal/<platform>/` plus `boards/<board>/`).
2. No chip-specific code outside ports and boards. The core is built and
   tested on Linux and macOS in CI on every commit.
3. The NXP i.MX RT1170 is the reference port: it is the GLA22's chip and the
   one NXP validated GenAVB on. The host simulator is the second port and the
   proof that the HAL is complete.
4. A second hardware port waits until after 1.0, unless a partner brings one.
   A porting guide ships with 1.0.

## Consequences

Phase 1 grows by about one week to specify the HAL. Every later hardware
feature is designed against the HAL first, then implemented in the RT1170
port.
