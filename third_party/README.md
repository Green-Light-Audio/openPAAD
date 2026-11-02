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

_None yet._ The planned changes for Milan v1.3 are listed in
`docs/requirements/README.md`.

## Updating GenAVB

```sh
scripts/update-genavb.sh <tag>
```
