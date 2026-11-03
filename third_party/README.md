# Third-party code

| Directory | Project | Version | Licence |
| --- | --- | --- | --- |
| `genavb/` | [NXP GenAVB/TSN](https://github.com/NXP/GenAVB_TSN) | 7.6.2 (tag `7_6_2`, e851a19) | BSD-3-Clause; `linux/modules` GPL-2.0 (never built by openPAAD) |

Other dependencies (MCUXpresso SDK, FreeRTOS kernel, NXP RTOS abstraction
layer, NXP rtos-apps) are fetched by west at the commits pinned in
`west.yml` and are not part of this repository.

## Local changes to GenAVB

GenAVB is vendored as a squashed git subtree (decision 0007). Changes made
by openPAAD, newest first:

| Date | File | Change | Why |
| --- | --- | --- | --- |
| 2026-11-03 | `avdecc/acmp.c` | Stream connect notifications to the application are zeroed before being filled | They were not, so fields left unset kept whatever the allocator returned: a listener's stream flags could carry random bits (media clock recovery, redundancy) |
| 2026-11-03 | `api/rtos/streaming.c` | `genavb_stream_send()` passes `media_api_write()` a zeroed safeguard entry after its one data entry | `media_api_write()` reads one entry past the last one it is given (as its own comment states); with a single entry that read was out of bounds (found by AddressSanitizer) |
| 2026-11-02 | `rtos/ipc.c` | Declare the IPC channels of a second endpoint port's gPTP and MAC service (`IPC_*_GPTP_1_*`, `IPC_*_MAC_SERVICE_1_*`) | GenAVB's gPTP and management code already select them for logical port 1, but the RTOS channel table lacked them, so a second endpoint gPTP instance could not start |
| 2026-11-02 | `include/genavb/net_types.h` | `htonll()` and `ntohll()` are only defined if the system has not defined them, like GenAVB already does for the 16- and 32-bit versions | macOS system headers define both; redefining them is an error with warnings as errors |
| 2026-11-02 | `rtos/config.h`, `rtos/net_port.h`, `rtos/net_port.c`, `rtos/net_logical_port.c` | New driver type `HAL_t` for endpoint ports whose driver lives outside GenAVB (`CFG_NUM_HAL_PORTS`); such ports report link changes with `port_link_event()` and their status with `hal_port_status()` | Lets openPAAD drive GenAVB through its HAL (decision 0010) without touching NXP's drivers; compiled out when a board has no HAL port |
| 2026-11-02 | `rtos/net_tx.c` | `leading_zeros()` uses `__builtin_clzl` when not compiling for Arm | The Arm `clz` instruction in inline assembly does not build on x86 or Apple hosts |
| 2026-11-02 | `rtos/include/osal/sys_types.h` | `__init`, `__exit` and `__ep_rx_buffers` are empty on macOS | ELF section names are invalid in Mach-O; placement only matters on embedded targets |

The planned changes for Milan v1.3 are listed in `docs/requirements/README.md`.

## Updating GenAVB

```sh
scripts/update-genavb.sh <tag>
```
