/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * The simulated board: the openPAAD HAL platform of openpaad-sim.
 *
 * Up to two network ports, each with its own free-running clock (offset and
 * natural frequency error chosen by the configuration) and its own timers.
 * Port 0 and port 1 can be joined by a virtual cable.
 */

#ifndef PAAD_SIM_PLATFORM_H
#define PAAD_SIM_PLATFORM_H

#include "paad/hal/platform.h"
#include "paad/sim/clock.h"
#include "paad/sim/eth.h"
#include "paad/sim/mclk.h"
#include "paad/sim/world.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAAD_SIM_PLATFORM_PORTS  2
#define PAAD_SIM_PLATFORM_TIMERS 4 /* per port clock */
#define PAAD_SIM_MCLK_HZ         24576000 /* 512 x 48 kHz */
#define PAAD_SIM_SAMPLE_RATE     48000

struct paad_sim_platform_config {
	enum paad_sim_time_mode mode;
	unsigned int num_ports;      /**< 1 or 2 */
	bool cable;                  /**< join port 0 and port 1 */
	uint32_t cable_ns;           /**< propagation delay of the cable */
	uint64_t initial_ns[PAAD_SIM_PLATFORM_PORTS];
	paad_hal_ppb_t natural_ppb[PAAD_SIM_PLATFORM_PORTS];
	uint8_t mac[PAAD_SIM_PLATFORM_PORTS][PAAD_HAL_ETH_ADDR_LEN];
	/** Media clock (24.576 MHz, 48 kHz audio, captures on port 0's
	 * clock): an oscillator of its own, with its own natural error. */
	bool mclk;
	paad_hal_ppb_t mclk_natural_ppb;
};

struct paad_sim_platform {
	struct paad_sim_world world;
	struct paad_sim_clock clock[PAAD_SIM_PLATFORM_PORTS];
	struct paad_sim_eth eth[PAAD_SIM_PLATFORM_PORTS];
	struct paad_sim_timer timer[PAAD_SIM_PLATFORM_PORTS][PAAD_SIM_PLATFORM_TIMERS];

	struct paad_hal_eth *eth_list[PAAD_SIM_PLATFORM_PORTS];
	struct paad_hal_timer *timer_list[PAAD_SIM_PLATFORM_PORTS * PAAD_SIM_PLATFORM_TIMERS];
	struct paad_sim_mclk mclk;
	struct paad_hal_platform hal;
	struct paad_sim_platform_config cfg;
};

/** Default configuration: real time, two ports joined by a 500 ns cable,
 * clocks 20 ppm apart, locally administered MAC addresses. */
void paad_sim_platform_default_config(struct paad_sim_platform_config *cfg);

/** Build the simulated board; paad_hal_platform_get() returns it afterwards. */
int paad_sim_platform_init(const struct paad_sim_platform_config *cfg);
struct paad_sim_platform *paad_sim_platform(void);

#ifdef __cplusplus
}
#endif

#endif /* PAAD_SIM_PLATFORM_H */
