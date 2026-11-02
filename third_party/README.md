# Third-party code

| Directory | Project | Version | Licence |
| --- | --- | --- | --- |
| 2026-11-02 | `rtos/config.h`, `rtos/net_port.h`, `rtos/net_port.c`, `rtos/net_logical_port.c` | New driver type `HAL_t` for endpoint ports whose driver lives outside GenAVB (`CFG_NUM_HAL_PORTS`); such ports report link changes with `port_link_event()` and their status with `hal_port_status()` | Lets openPAAD drive GenAVB through its HAL (decision 0010) without touching NXP's drivers; compiled out when a board has no HAL port |
| `genavb/` | [NXP GenAVB/TSN](https://github.com/NXP/GenAVB_TSN) | 7.6.2 (tag `7_6_2`, e851a19) | BSD-3-Clause; `linux/modules` GPL-2.0 (never built by openPAAD) |

Other dependencies (MCUXpresso SDK, FreeRTOS kernel, NXP RTOS abstraction
layer, NXP rtos-apps) are fetched by west at the commits pinned in
`west.yml` and are not part of this repository.

## Local changes to GenAVB

GenAVB is vendored as a squashed git subtree (decision 0007). Changes made
by openPAAD, newest first:

| Date | File | Change | Why |
| --- | --- | --- | --- |
| 2026-11-02 | `rtos/config.h`, `rtos/net_port.h`, `rtos/net_port.c`, `rtos/net_logical_port.c` | New driver type `HAL_t` for endpoint ports whose driver lives outside GenAVB (`CFG_NUM_HAL_PORTS`); such ports report link changes with `port_link_event()` and their status with `hal_port_status()` | Lets openPAAD drive GenAVB through its HAL (decision 0010) without touching NXP's drivers; compiled out when a board has no HAL port |
| 2026-11-02 | `rtos/net_tx.c` | `leading_zeros()` uses `__builtin_clzl` when not compiling for Arm | The Arm `clz` instruction in inline assembly does not build on x86 or Apple hosts |
| 2026-11-02 | `rtos/include/osal/sys_types.h` | `__init`, `__exit` and `__ep_rx_buffers` are empty on macOS | ELF section names are invalid in Mach-O; placement only matters on embedded targets |

The planned changes for Milan v1.3 are listed in `docs/requirements/README.md`.

## Updating GenAVB

```sh
scripts/update-genavb.sh <tag>
```
