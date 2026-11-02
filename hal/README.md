# Hardware abstraction layer (HAL)

openPAAD is portable by design: everything above this directory is plain C
with no chip-specific code, and everything a platform must provide goes
through the HAL. Decision 0009 explains why.

## What a port provides

| Service | What it covers | Why it cannot be generic |
| --- | --- | --- |
| Network port | Frame transmit and receive, hardware timestamps on both directions, per-class transmit queues | Timestamp accuracy sets gPTP accuracy |
| Traffic shaping | Credit-based shaper for stream classes (802.1Qav), or an equivalent | Frames must leave every 125 us without software jitter |
| Time | A gPTP-adjustable hardware clock: read, adjust frequency, step | Lives in the Ethernet MAC or a PHC |
| Timers | High-resolution timers and timestamp capture | Media clock recovery measures the audio clock against gPTP time |
| Media clock | Generate the audio clock and steer its frequency (fractional PLL, or a fallback such as ASRC) | Audio PLLs differ on every chip |
| Audio I/O | Sample transfer to converters or codecs (SAI/I2S/TDM) by DMA | Peripheral and codec specific |
| Storage | Small persistent store for names, bindings, IDs | Flash layout specific |
| Identity | MAC address and unique ID source | Fuses, EEPROM or OTP |

The HAL is specified as C headers in Phase 1. Today the ports plug in at
GenAVB's OS layer (`genavb_sdk.h` and the functions behind it); the Phase 1
HAL wraps that boundary so a port never has to read GenAVB internals.

## Ports

| Directory | Platform | State |
| --- | --- | --- |
| `rt1170/` (+ `boards/evkb-rt1170/`) | NXP i.MX RT1170, FreeRTOS | Reference port: builds, bring-up in Phase 2 |
| `sim/` | Linux and macOS host, FreeRTOS POSIX port | Protocol code builds; network and clock simulation in Phase 1 |

Candidates for a next hardware port, to be evaluated after 1.0: Linux on a
PC with an Intel i210 or i226 network card (hardware credit-based shaping),
and microcontroller families with an AVB-capable Ethernet MAC from other
vendors. A porting guide arrives with 1.0.
