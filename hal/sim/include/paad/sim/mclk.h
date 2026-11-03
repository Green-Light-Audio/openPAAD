/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * Simulated media clock (HAL port: sim).
 *
 * A model of a board's audio clock: an oscillator with its own natural
 * error (audio oscillators are no more exact than network crystals), a
 * frequency adjustment in ppb like a fractional PLL, and a free-running
 * counter of its ticks. A capture reads the counter at the instant a gPTP
 * clock reaches a given time, as a timer capture input fed by a 1588 timer
 * compare output would.
 *
 * The counter is computed, not stepped: its value at any source time is
 * exact to a fraction of a tick, which makes it a reference to measure
 * media clock recovery against (paad_sim_mclk_count_at).
 */

#ifndef PAAD_SIM_MCLK_H
#define PAAD_SIM_MCLK_H

#include <pthread.h>

#include "paad/hal/mclk.h"
#include "paad/sim/clock.h"
#include "paad/sim/world.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAAD_SIM_MCLK_MAX_ADJUST_PPB 1000000 /* 1000 ppm, like a fractional audio PLL */

/** A media clock count with its fraction. */
struct paad_sim_mclk_count {
	uint64_t ticks;
	uint32_t frac; /**< in 1/2^32 tick */
};

struct paad_sim_mclk {
	struct paad_hal_mclk hal; /**< the HAL object, first member */
	struct paad_sim_world *world;
	struct paad_sim_clock *clock; /**< the gPTP clock captures refer to */
	pthread_mutex_t lock;

	uint64_t ref_source_ns;     /**< source time at the last rebase */
	struct paad_sim_mclk_count ref; /**< count at the last rebase */
	paad_hal_ppb_t ppb;         /**< current adjustment */
	paad_hal_ppb_t natural_ppb; /**< built-in error of the oscillator */

	bool capture_pending;
	uint64_t capture_ns; /**< on the gPTP clock */
	unsigned int captures;
	unsigned int adjusts;
};

/** Initialise a media clock of tick_hz (e.g. 24576000) for audio at
 * sample_rate, with captures on clock. The counter starts at zero. */
int paad_sim_mclk_init(struct paad_sim_mclk *mclk, struct paad_sim_world *world, struct paad_sim_clock *clock,
		       uint32_t tick_hz, uint32_t sample_rate, paad_hal_ppb_t natural_ppb);
void paad_sim_mclk_deinit(struct paad_sim_mclk *mclk);

/** Counter value at source time source_ns (exact for any time since the
 * last adjustment; earlier times are extrapolated at the current rate). */
struct paad_sim_mclk_count paad_sim_mclk_count_at(struct paad_sim_mclk *mclk, uint64_t source_ns);

/** Current adjustment, in ppb. */
paad_hal_ppb_t paad_sim_mclk_adjustment(struct paad_sim_mclk *mclk);

#ifdef __cplusplus
}
#endif

#endif /* PAAD_SIM_MCLK_H */
