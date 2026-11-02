/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Board services required by the GenAVB/TSN OS layer on the MIMXRT1170-EVKB
 * (declared in genavb_sdk.h), plus the clock set-up they rely on.
 *
 * Clock plan (verify on hardware in Phase 2/3, see docs/hardware/evkb-rt1170.md):
 *   - ENET_1G core (ENET2 root):   SYS_PLL1_DIV2 (500 MHz) / 4 = 125 MHz (RGMII)
 *   - ENET_1G 1588 timer (ENET_TIMER2 root): SYS_PLL1_DIV2 / 2 = 250 MHz
 *   - Audio PLL:                    24 MHz * (32 + 96/125) / 2 = 393.216 MHz
 *   - GPT1 (stack timers):          24 MHz crystal oscillator
 *   - GPT2 (media clock recovery):  Audio PLL / 16 = 24.576 MHz
 */

#include <string.h>

#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"

#include "genavb_sdk.h"
#include "openpaad/board.h"

#define AUDIO_PLL_LOOP_DIVIDER 32U
#define AUDIO_PLL_POST_DIVIDER 1U /* register encoding: divide by 2 */
#define AUDIO_PLL_NUMERATOR    96U
#define AUDIO_PLL_DENOMINATOR  125U

#define GPT2_ROOT_DIVIDER 16U

#define PHY0_RESET_GPIO GPIO11
#define PHY0_RESET_PIN  14U

void openpaad_board_clocks_init(void)
{
	clock_root_config_t root;

	const clock_sys_pll1_config_t sys_pll1 = {
		.pllDiv2En = true,
	};
	CLOCK_InitSysPll1(&sys_pll1);

	/* ENET_1G core clock, 125 MHz for RGMII at 1 Gbit/s. */
	root.clockOff = false;
	root.mux = kCLOCK_ENET2_ClockRoot_MuxSysPll1Div2;
	root.div = 4;
	CLOCK_SetRootClock(kCLOCK_Root_Enet2, &root);

	/* ENET_1G IEEE 1588 timer, 250 MHz (4 ns resolution). */
	root.mux = kCLOCK_ENET_TIMER2_ClockRoot_MuxSysPll1Div2;
	root.div = 2;
	CLOCK_SetRootClock(kCLOCK_Root_Enet_Timer2, &root);

	/* Audio PLL for the 48 kHz family, adjusted at run time by media clock
	 * recovery through its fractional numerator. */
	const clock_audio_pll_config_t audio_pll = {
		.loopDivider = AUDIO_PLL_LOOP_DIVIDER,
		.postDivider = AUDIO_PLL_POST_DIVIDER,
		.numerator = AUDIO_PLL_NUMERATOR,
		.denominator = AUDIO_PLL_DENOMINATOR,
		.ss = NULL,
		.ssEnable = false,
	};
	CLOCK_InitAudioPll(&audio_pll);

	/* GPT1: stack high-resolution timers, from the 24 MHz oscillator. */
	root.mux = kCLOCK_GPT1_ClockRoot_MuxOsc24MOut;
	root.div = 1;
	CLOCK_SetRootClock(kCLOCK_Root_Gpt1, &root);

	/* GPT2: counts the audio clock for media clock recovery. */
	root.mux = kCLOCK_GPT2_ClockRoot_MuxAudioPllOut;
	root.div = GPT2_ROOT_DIVIDER;
	CLOCK_SetRootClock(kCLOCK_Root_Gpt2, &root);
}

void openpaad_board_enet_init(void)
{
	/* RGMII mode for ENET_1G, transmit clock generated internally. */
	IOMUXC_GPR->GPR5 |= IOMUXC_GPR_GPR5_ENET1G_RGMII_EN_MASK;

	/* Errata ERR050396: ENET writes to CM7 TCM must not go through the
	 * cache path; buffers live in OCRAM non-cacheable memory instead. */
	IOMUXC_GPR->GPR28 &= ~IOMUXC_GPR_GPR28_CACHE_ENET1G_MASK;

	/* Route an ENET_1G 1588 timer output to GPT2 capture input 1, so media
	 * clock recovery can timestamp gPTP time against the audio clock
	 * without external wiring. Verify on hardware. */
	IOMUXC_GPR->GPR23 |= IOMUXC_GPR_GPR23_GPT2_CAPIN1_SEL_MASK;

	gpio_pin_config_t out = { kGPIO_DigitalOutput, 1, kGPIO_NoIntmode };
	GPIO_PinInit(PHY0_RESET_GPIO, PHY0_RESET_PIN, &out);

	EnableIRQ(ENET_1G_MAC0_Tx_Rx_1_IRQn);
	EnableIRQ(ENET_1G_MAC0_Tx_Rx_2_IRQn);
}

void openpaad_board_phy0_reset(uint8_t phy_addr)
{
	(void)phy_addr;

	/* RTL8211FDI: hold reset low for at least 10 ms, then wait 30 ms for
	 * the internal circuits to settle before touching PHY registers. */
	GPIO_PinWrite(PHY0_RESET_GPIO, PHY0_RESET_PIN, 0);
	SDK_DelayAtLeastUs(10000, CLOCK_GetFreq(kCLOCK_CpuClk));
	GPIO_PinWrite(PHY0_RESET_GPIO, PHY0_RESET_PIN, 1);
	SDK_DelayAtLeastUs(30000, CLOCK_GetFreq(kCLOCK_CpuClk));
}

/* --- GPT ------------------------------------------------------------------ */

gpt_clock_source_t BOARD_GPT_clk_src(GPT_Type *base)
{
	(void)base;
	return kGPT_ClockSource_Periph;
}

uint32_t BOARD_GPT_clk_freq(GPT_Type *base)
{
	if (base == GPT2)
		return CLOCK_GetRootClockFreq(kCLOCK_Root_Gpt2);

	return CLOCK_GetRootClockFreq(kCLOCK_Root_Gpt1);
}

uint32_t BOARD_GPT_clk_src_div(GPT_Type *base)
{
	/* Ratio between the audio PLL output and the clock the recovery GPT
	 * counts. Only meaningful for the recovery timer. */
	if (base == GPT2)
		return GPT2_ROOT_DIVIDER;

	return 1;
}

/* --- ENET ----------------------------------------------------------------- */

uint32_t dev_get_enet_core_freq(void *base)
{
	if (base == (void *)ENET_1G)
		return CLOCK_GetRootClockFreq(kCLOCK_Root_Bus);

	return CLOCK_GetRootClockFreq(kCLOCK_Root_Bus);
}

uint32_t dev_get_enet_1588_freq(void *base)
{
	if (base == (void *)ENET_1G)
		return CLOCK_GetRootClockFreq(kCLOCK_Root_Enet_Timer2);

	return CLOCK_GetRootClockFreq(kCLOCK_Root_Enet_Timer1);
}

int BOARD_NetPort_Get_MAC(unsigned int port, uint8_t *mac)
{
	/* Locally administered address. Phase 2 replaces the low bytes with
	 * bytes from the chip's unique ID so that two boards never collide. */
	static const uint8_t base_mac[6] = { 0x02, 0x00, 0x5A, 0xAD, 0x00, 0x00 };

	if (!mac)
		return -1;

	memcpy(mac, base_mac, sizeof(base_mac));
	mac[5] = (uint8_t)port;

	return 0;
}

/* --- Audio PLL (media clock recovery) -------------------------------------- */

uint32_t dev_get_pll_ref_freq(void)
{
	return CLOCK_GetFreq(kCLOCK_Osc24MOut);
}

uint32_t dev_read_audio_pll_num(void)
{
	return ANADIG_PLL->PLL_AUDIO_NUMERATOR & ANADIG_PLL_PLL_AUDIO_NUMERATOR_NUM_MASK;
}

uint32_t dev_read_audio_pll_denom(void)
{
	return ANADIG_PLL->PLL_AUDIO_DENOMINATOR & ANADIG_PLL_PLL_AUDIO_DENOMINATOR_DENOM_MASK;
}

uint32_t dev_read_audio_pll_post_div(void)
{
	/* GenAVB names this "post_div" but uses it as the integer part of the
	 * PLL multiplier, i.e. the loop divider. */
	return ANADIG_PLL->PLL_AUDIO_DIV_SELECT & ANADIG_PLL_PLL_AUDIO_DIV_SELECT_PLL_AUDIO_DIV_SELECT_MASK;
}

void dev_write_audio_pll_num(uint32_t num)
{
	ANADIG_PLL->PLL_AUDIO_NUMERATOR = ANADIG_PLL_PLL_AUDIO_NUMERATOR_NUM(num);
}
