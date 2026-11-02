/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>

#include "paad/sim/platform.h"

static struct paad_sim_platform sim;
static bool initialised;

void paad_sim_platform_default_config(struct paad_sim_platform_config *cfg)
{
	unsigned int i;

	memset(cfg, 0, sizeof(*cfg));
	cfg->mode = PAAD_SIM_REAL_TIME;
	cfg->num_ports = 2;
	cfg->cable = true;
	cfg->cable_ns = 500;

	for (i = 0; i < PAAD_SIM_PLATFORM_PORTS; i++) {
		static const uint8_t base[PAAD_HAL_ETH_ADDR_LEN] = { 0x02, 0x00, 0x5a, 0xad, 0x00, 0x00 };

		memcpy(cfg->mac[i], base, sizeof(base));
		cfg->mac[i][5] = (uint8_t)(i + 1);
		cfg->initial_ns[i] = (uint64_t)(i + 1) * 1000000000ULL * 1000; /* 1000 s, 2000 s */
	}
	cfg->natural_ppb[0] = 0;
	cfg->natural_ppb[1] = 20000; /* 20 ppm: crystals are never equal */
}

static int platform_get_mac(unsigned int eth_index, uint8_t mac[PAAD_HAL_ETH_ADDR_LEN])
{
	if (eth_index >= sim.cfg.num_ports)
		return PAAD_HAL_ERR_INVALID;

	memcpy(mac, sim.cfg.mac[eth_index], PAAD_HAL_ETH_ADDR_LEN);

	return PAAD_HAL_OK;
}

int paad_sim_platform_init(const struct paad_sim_platform_config *cfg)
{
	unsigned int i, t, n = 0;
	int rc;

	if (initialised || cfg->num_ports < 1 || cfg->num_ports > PAAD_SIM_PLATFORM_PORTS ||
	    (cfg->cable && cfg->num_ports < 2))
		return PAAD_HAL_ERR_INVALID;

	memset(&sim, 0, sizeof(sim));
	sim.cfg = *cfg;

	rc = paad_sim_world_init(&sim.world, cfg->mode);
	if (rc != PAAD_HAL_OK)
		return rc;

	for (i = 0; i < cfg->num_ports; i++) {
		static const char *const names[PAAD_SIM_PLATFORM_PORTS] = { "sim0", "sim1" };

		rc = paad_sim_clock_init(&sim.clock[i], paad_sim_world_now, &sim.world, cfg->initial_ns[i],
					 cfg->natural_ppb[i]);
		if (rc != PAAD_HAL_OK)
			return rc;

		rc = paad_sim_eth_init(&sim.eth[i], &sim.world, &sim.clock[i], names[i]);
		if (rc != PAAD_HAL_OK)
			return rc;
		sim.eth_list[i] = &sim.eth[i].hal;

		for (t = 0; t < PAAD_SIM_PLATFORM_TIMERS; t++) {
			rc = paad_sim_timer_init(&sim.timer[i][t], &sim.world, &sim.clock[i]);
			if (rc != PAAD_HAL_OK)
				return rc;
			sim.timer_list[n++] = &sim.timer[i][t].hal;
		}
	}

	if (cfg->cable) {
		rc = paad_sim_eth_connect(&sim.eth[0], &sim.eth[1], cfg->cable_ns);
		if (rc != PAAD_HAL_OK)
			return rc;
	}

	sim.hal.name = "openpaad-sim";
	sim.hal.eth = sim.eth_list;
	sim.hal.num_eth = cfg->num_ports;
	sim.hal.timers = sim.timer_list;
	sim.hal.num_timers = n;
	sim.hal.get_mac = platform_get_mac;

	initialised = true;

	return PAAD_HAL_OK;
}

struct paad_sim_platform *paad_sim_platform(void)
{
	return initialised ? &sim : NULL;
}

const struct paad_hal_platform *paad_hal_platform_get(void)
{
	return initialised ? &sim.hal : NULL;
}

/* GenAVB asks the board for each logical port's MAC address. */
int BOARD_NetPort_Get_MAC(unsigned int port, uint8_t *mac);

int BOARD_NetPort_Get_MAC(unsigned int port, uint8_t *mac)
{
	return platform_get_mac(port, mac) == PAAD_HAL_OK ? 0 : -1;
}
