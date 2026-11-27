/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * GenAVB/TSN board configuration for the NXP MIMXRT1170-EVKB (Cortex-M7).
 *
 * GenAVB's RTOS OS layer reads its hardware description from a header named
 * genavb_sdk.h (see third_party/genavb/doc/rtos_compile_time_config.md).
 * NXP ships its own version only inside a downloadable demo package; this
 * file is written from the public documentation, the public SDK examples for
 * the EVKB and the EVKB schematics, so the stack builds from public sources.
 *
 * Values marked "verify on hardware" are educated choices that Phase 2
 * (board bring-up) must confirm with measurements; see
 * docs/hardware/evkb-rt1170.md.
 */

#ifndef OPENPAAD_EVKB_RT1170_GENAVB_SDK_H
#define OPENPAAD_EVKB_RT1170_GENAVB_SDK_H

#include <stdint.h>
#include "fsl_device_registers.h"
#include "fsl_enet.h"
#include "fsl_gpt.h"

/* -------------------------------------------------------------------------
 * Network receive scheduling
 * ------------------------------------------------------------------------- */

/* Packets processed per receive-task period (the base period is 125 us). */
#define BOARD_NET_RX_PACKETS     8
#define BOARD_NET_RX_PERIOD_MULT 1

/* -------------------------------------------------------------------------
 * Network ports
 *
 * Port 0 is the ENET_1G controller: RGMII to the Realtek RTL8211FDI PHY,
 * the board's "1G Ethernet" jack. It is the only controller on the RT1170
 * with the AVB credit-based shapers needed for streaming.
 * ------------------------------------------------------------------------- */

#define BOARD_NUM_PORTS      1
#define BOARD_NUM_ENET_PORTS 1

#define BOARD_NET_PORT0_DRV_TYPE          ENET_1G_t
#define BOARD_NET_PORT0_DRV_INDEX         0
#define BOARD_NET_PORT0_DRV_BASE          ENET_1G_BASE
#define BOARD_NET_PORT0_PHY_INDEX         0
#define BOARD_NET_PORT0_MII_MODE          kENET_RgmiiMode
#define BOARD_NET_PORT0_HW_CLOCK          0
#define BOARD_NET_PORT0_TRAFFIC_CLASS_MAX 3 /* ENET_1G has 3 transmit queues */
#define BOARD_NET_PORT0_SR_CLASS_MAX      2 /* class A and class B */

/* ENET_1G IEEE 1588 timer channels used by the stack, in GenAVB order:
 * [0] media clock generation, [1] media clock recovery capture,
 * [2] spare / PPS. Verify on hardware against the GPT capture routing below. */
#define BOARD_ENET0_1588_TIMER_CHANNEL_0 kENET_PtpTimerChannel1
#define BOARD_ENET0_1588_TIMER_CHANNEL_1 kENET_PtpTimerChannel2
#define BOARD_ENET0_1588_TIMER_CHANNEL_2 kENET_PtpTimerChannel3

/* Received frames are placed in non-cacheable memory by the linker script;
 * do not define BOARD_NET_RX_CACHEABLE / BOARD_NET_TX_CACHEABLE. */

/* -------------------------------------------------------------------------
 * MDIO and PHY
 * ------------------------------------------------------------------------- */

#define BOARD_NUM_MDIO       1
#define BOARD_MDIO0_DRV_TYPE  ENET_1G_t
#define BOARD_MDIO0_DRV_INDEX 0

#define BOARD_NUM_PHY        1
#define BOARD_PHY0_MDIO_ID   0
#define BOARD_PHY0_ADDRESS   0x01U        /* RTL8211FDI strap address on the EVKB */
#define BOARD_PHY0_OPS       phyrtl8211f_ops
#define BOARD_PHY0_RESET     openpaad_board_phy0_reset

/* PHY timestamping latencies in ns, used to correct hardware timestamps for
 * the delay between the MAC timestamp point and the wire. Starting values
 * from the RTL8211F datasheet; verify on hardware with a reference
 * grandmaster (Phase 2 exit gate). */
#define BOARD_PHY0_RX_LATENCY_100M 512
#define BOARD_PHY0_TX_LATENCY_100M 384
#define BOARD_PHY0_RX_LATENCY_1G   312
#define BOARD_PHY0_TX_LATENCY_1G   184

/* -------------------------------------------------------------------------
 * General purpose timers
 *
 * GPT1 drives the stack's high-resolution timers. GPT2 timestamps the media
 * clock for recovery: its input capture channel 1 can be fed internally from
 * an ENET_1G 1588 timer output (IOMUXC_GPR23 GPT2_CAPIN1_SEL), so recovery
 * needs no external wiring. Verify on hardware.
 * ------------------------------------------------------------------------- */

#define BOARD_NUM_GPT 2

#define BOARD_GPT_0_BASE        GPT1
#define BOARD_GPT_0_IRQ         GPT1_IRQn
#define BOARD_GPT_0_IRQ_HANDLER GPT1_IRQHandler

#define BOARD_GPT_1_BASE        GPT2
#define BOARD_GPT_1_IRQ         GPT2_IRQn
#define BOARD_GPT_1_IRQ_HANDLER GPT2_IRQHandler

#define BOARD_GPT_REC_BASE    GPT2
#define BOARD_GPT_REC_CHANNEL 1

/* -------------------------------------------------------------------------
 * Board services implemented in board_genavb.c
 * ------------------------------------------------------------------------- */

/* Clock source, frequency and source divider seen by a GPT instance. */
gpt_clock_source_t BOARD_GPT_clk_src(GPT_Type *base);
uint32_t BOARD_GPT_clk_freq(GPT_Type *base);
uint32_t BOARD_GPT_clk_src_div(GPT_Type *base);

/* ENET core and 1588 timer clock frequencies, in Hz. */
uint32_t dev_get_enet_core_freq(void *base);
uint32_t dev_get_enet_1588_freq(void *base);

/* MAC address of a logical network port (6 bytes). Returns 0 or -1. */
int BOARD_NetPort_Get_MAC(unsigned int port, uint8_t *mac);

/* Hardware reset of PHY 0 (RTL8211FDI reset line on GPIO11 pin 14). */
void openpaad_board_phy0_reset(uint8_t phy_addr);

/* Audio PLL access for media clock recovery (rtos/imx-pll.c). The PLL is
 * adjusted by rewriting its fractional numerator. */
uint32_t dev_get_pll_ref_freq(void);
uint32_t dev_read_audio_pll_num(void);
uint32_t dev_read_audio_pll_denom(void);
uint32_t dev_read_audio_pll_post_div(void);
void dev_write_audio_pll_num(uint32_t num);

#endif /* OPENPAAD_EVKB_RT1170_GENAVB_SDK_H */
