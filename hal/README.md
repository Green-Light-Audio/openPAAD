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

The HAL is specified as C headers in [`include/paad/hal/`](include/paad/hal/)
(version 0.1; design in decision 0010):

| Header | Service |
| --- | --- |
| `common.h` | Return codes, time and context conventions |
| `clock.h` | gPTP hardware clock: read, frequency adjust, step |
| `timer.h` | Event timers on the gPTP clock |
| `eth.h` | Ethernet port: timestamped transmit and receive, multicast, credit-based shaping |
| `mclk.h` | Media clock: steer the audio clock, capture it at gPTP instants |
| `audio.h` | Audio input and output, period by period |
| `storage.h` | Persistent key-value settings |
| `platform.h` | The platform: its service instances and identity sources |

A generic GenAVB port driver built on these headers connects them to the
stack (Phase 1). It replaces only GenAVB's hardware drivers: on the host,
90 of the 95 files of GenAVB's OS layer already build unchanged with
warnings as errors (GCC and Clang). The five that need the HAL are the ENET
driver (`net_port_enet*.c`), the gPTP event timer glue (`gptp_dev.c`) and
the i.MX audio PLL driver (`imx-pll.c`). Media clock recovery uses GenAVB's
own control loop on the HAL media clock (`paad/genavb/mclk_rec.c`, decision
0014). The RT1170 still plugs in at GenAVB's own OS layer (`genavb_sdk.h`)
until its HAL port is written.

## Ports

| Directory | Platform | State |
| --- | --- | --- |
| `rt1170/` (+ `boards/evkb-rt1170/`) | NXP i.MX RT1170, FreeRTOS | Reference port: builds, bring-up in Phase 2 |
| `sim/` | Linux and macOS host, FreeRTOS POSIX port | Clock, timers, Ethernet and media clock simulated (virtual or real time); ports cabled within a process, between processes, or plugged into a host interface (libpcap); runs the full GenAVB stack in `openpaad-sim`; audio I/O in Phase 1 |

Candidates for a next hardware port, to be evaluated after 1.0: Linux on a
PC with an Intel i210 or i226 network card (hardware credit-based shaping),
and microcontroller families with an AVB-capable Ethernet MAC from other
vendors. A porting guide arrives with 1.0.
