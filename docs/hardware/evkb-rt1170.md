# MIMXRT1170-EVKB: hardware notes

Reference board for openPAAD: NXP MIMXRT1170-EVKB (i.MX RT1176, Cortex-M7 at
1 GHz plus Cortex-M4). openPAAD runs on the Cortex-M7, executing in place from
the board's QSPI flash.

## What openPAAD uses

| Function | Hardware | Notes |
| --- | --- | --- |
| AVB network port | ENET_1G, RGMII to the RTL8211FDI PHY (MDIO address 1) | Only RT1170 MAC with AVB credit-based shapers and 3 transmit queues |
| PHY reset | GPIO11 pin 14 | 10 ms low, 30 ms settle |
| gPTP time | ENET_1G IEEE 1588 timer, clocked at 250 MHz | |
| Stack timers | GPT1 from the 24 MHz oscillator | |
| Media clock recovery | GPT2 counting the audio PLL / 16, capture input 1 fed from an ENET_1G 1588 timer output (IOMUXC_GPR23) | |
| Audio clock | Audio PLL at 393.216 MHz, fractional numerator adjusted at run time | |
| Debug console | LPUART1 through the on-board MCU-Link (USB) | |
| Audio codec (Phase 3) | WM8962 on SAI1 | |

## To verify on hardware (Phase 2 and 3)

Values in `boards/evkb-rt1170/include/genavb_sdk.h` and
`src/board_genavb.c` that are reasoned from documentation but not yet
measured:

- [ ] PHY timestamp latencies (RX/TX at 100 Mbit/s and 1 Gbit/s), measured
      against a reference grandmaster.
- [ ] ENET_1G 1588 timer channel assignment and the internal routing to GPT2
      capture input 1.
- [ ] GPT2 clocked from the audio PLL: divider and the `BOARD_GPT_clk_src_div`
      semantics expected by `gpt_rec.c`.
- [ ] ENET_1G core clock reported to the driver (`dev_get_enet_core_freq`).
- [ ] Audio PLL numerator writes take effect without a lock loss.
- [ ] MAC address derived from the chip's unique ID instead of the fixed
      locally administered address.
- [ ] Pin mux: today the NXP ENET_1G example pin mux is used; Phase 3 adds SAI1
      and the codec I2C and needs a combined pin mux.

## Memory map (reference endpoint, Phase 0)

| Region | Use | Size used |
| --- | --- | --- |
| QSPI flash (XIP) | code and constants | about 377 KB |
| DTCM (256 KB) | stack data, DMA descriptors and buffers (not cached) | about 93 KB |
| OCRAM (512 KB) | FreeRTOS heap (256 KB, cached) | 256 KB |

ENET DMA buffers stay in DTCM, which is not cached, and IOMUXC_GPR28
CACHE_ENET1G is cleared (NXP erratum ERR050396).
