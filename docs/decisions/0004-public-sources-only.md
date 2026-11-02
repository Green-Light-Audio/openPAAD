# 0004. Build from public sources only

- Status: accepted
- Date: 2026-11-02

## Context

NXP's FreeRTOS build of GenAVB expects an application tree (`RTOS_APPS`)
holding board configuration headers and demo code for the RT1170 EVKB. That
tree ships only in a downloadable package from nxp.com, not on GitHub. A
project that needs it cannot have reproducible public CI.

## Decision

Everything needed to build comes from public Git repositories, pinned by
commit in `west.yml`:

- MCUXpresso SDK 26.03.00 (drivers, device files, CMSIS, FreeRTOS kernel,
  components, and the EVKB board files from the examples repository);
- NXP RTOS abstraction layer 1.7.0 and rtos-apps 1.3.0;
- GenAVB/TSN 7.6.2, vendored (see 0002).

openPAAD writes what the downloadable package would have provided:
`genavb_sdk.h`, `FreeRTOSConfig.h`, board services, linker script and the
application. The values are derived from GenAVB's documentation
(`doc/rtos_compile_time_config.md`), the public SDK examples and the EVKB
schematics; open points are listed in `docs/hardware/evkb-rt1170.md`.

## Consequences

Anyone can build the firmware from a fresh clone. The board configuration
needs hardware validation that NXP's package would have given us (Phase 2).
