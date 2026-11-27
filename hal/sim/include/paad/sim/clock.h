/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * Simulated gPTP hardware clock (HAL port: sim).
 *
 * A nanosecond clock derived from a monotonic time source, with the same
 * frequency adjustment and step behaviour as a hardware 1588 timer. Each
 * simulated endpoint has its own clock, so gPTP has real work to do: by
 * default the source is the host's CLOCK_MONOTONIC, and tests inject their
 * own source to run deterministically.
 */

#ifndef PAAD_SIM_CLOCK_H
#define PAAD_SIM_CLOCK_H

#include <pthread.h>

#include "paad/hal/clock.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Monotonic time source in ns. */
typedef uint64_t (*paad_sim_time_source)(void *ctx);

struct paad_sim_clock {
	struct paad_hal_clock hal;      /**< the HAL object, first member */

	paad_sim_time_source source;
	void *source_ctx;

	pthread_mutex_t lock;
	uint64_t ref_source_ns; /**< source time at the last rebase */
	uint64_t ref_clock_ns;  /**< clock time at the last rebase */
	paad_hal_ppb_t ppb;     /**< current frequency offset */
	paad_hal_ppb_t natural_ppb; /**< built-in error of this "crystal" */
};

/**
 * Initialise a simulated clock.
 * @param source      time source; NULL means the host's CLOCK_MONOTONIC
 * @param initial_ns  clock value at initialisation
 * @param natural_ppb frequency error of the simulated oscillator, so two
 *                    simulated endpoints drift apart like real ones
 */
int paad_sim_clock_init(struct paad_sim_clock *clk, paad_sim_time_source source, void *source_ctx,
			uint64_t initial_ns, paad_hal_ppb_t natural_ppb);

void paad_sim_clock_deinit(struct paad_sim_clock *clk);

/** Host CLOCK_MONOTONIC in ns, the default source. */
uint64_t paad_sim_monotonic_ns(void *ctx);

/** Clock value at a given source time, with the clock's current rate. */
uint64_t paad_sim_clock_at(struct paad_sim_clock *clk, uint64_t source_ns);

/** Earliest source time at which the clock reads at least clock_ns, with
 * the clock's current rate. May be in the past. */
uint64_t paad_sim_clock_source_at(struct paad_sim_clock *clk, uint64_t clock_ns);

#ifdef __cplusplus
}
#endif

#endif /* PAAD_SIM_CLOCK_H */
