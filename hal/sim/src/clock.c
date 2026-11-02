/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <time.h>

#include "paad/sim/clock.h"

/* Largest frequency offset accepted, like a typical 1588 timer: 500 ppm. */
#define SIM_CLOCK_MAX_ADJUST_PPB 500000

uint64_t paad_sim_monotonic_ns(void *ctx)
{
	struct timespec ts;

	(void)ctx;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (uint64_t)ts.tv_sec * PAAD_HAL_NSEC_PER_SEC + (uint64_t)ts.tv_nsec;
}

/* Clock time for a given source time, at the current rate. Caller holds the
 * lock. The effective rate is nominal + natural error + adjustment. The
 * product needs 128-bit arithmetic, available on the 64-bit hosts the
 * simulator targets. */
static uint64_t clock_at(const struct paad_sim_clock *c, uint64_t source_ns)
{
	int64_t ppb = (int64_t)c->natural_ppb + c->ppb;
	uint64_t elapsed = source_ns - c->ref_source_ns;
	__int128 scaled = (__int128)elapsed * (int64_t)(PAAD_HAL_NSEC_PER_SEC + ppb) / (int64_t)PAAD_HAL_NSEC_PER_SEC;

	return c->ref_clock_ns + (uint64_t)scaled;
}

/* Move the reference point to now, so that a rate change applies from now
 * on and the clock stays continuous. Caller holds the lock. */
static uint64_t rebase(struct paad_sim_clock *c)
{
	uint64_t source_ns = c->source(c->source_ctx);

	c->ref_clock_ns = clock_at(c, source_ns);
	c->ref_source_ns = source_ns;

	return c->ref_clock_ns;
}

static uint64_t sim_read_ns(struct paad_hal_clock *hal)
{
	struct paad_sim_clock *c = (struct paad_sim_clock *)hal;
	uint64_t now;

	pthread_mutex_lock(&c->lock);
	now = clock_at(c, c->source(c->source_ctx));
	pthread_mutex_unlock(&c->lock);

	return now;
}

static int sim_adjust_frequency(struct paad_hal_clock *hal, paad_hal_ppb_t ppb)
{
	struct paad_sim_clock *c = (struct paad_sim_clock *)hal;

	if (ppb > hal->max_adjust_ppb)
		ppb = hal->max_adjust_ppb;
	else if (ppb < -hal->max_adjust_ppb)
		ppb = -hal->max_adjust_ppb;

	pthread_mutex_lock(&c->lock);
	rebase(c);
	c->ppb = ppb;
	pthread_mutex_unlock(&c->lock);

	return PAAD_HAL_OK;
}

static int sim_step(struct paad_hal_clock *hal, int64_t offset_ns)
{
	struct paad_sim_clock *c = (struct paad_sim_clock *)hal;
	int rc = PAAD_HAL_OK;
	uint64_t now;

	pthread_mutex_lock(&c->lock);
	now = rebase(c);
	if (offset_ns < 0 && (uint64_t)(-offset_ns) > now)
		rc = PAAD_HAL_ERR_INVALID; /* would go before zero */
	else
		c->ref_clock_ns = now + (uint64_t)offset_ns;
	pthread_mutex_unlock(&c->lock);

	return rc;
}

static const struct paad_hal_clock_ops sim_clock_ops = {
	.read_ns = sim_read_ns,
	.adjust_frequency = sim_adjust_frequency,
	.step = sim_step,
};

int paad_sim_clock_init(struct paad_sim_clock *clk, paad_sim_time_source source, void *source_ctx,
			uint64_t initial_ns, paad_hal_ppb_t natural_ppb)
{
	if (!clk)
		return PAAD_HAL_ERR_INVALID;

	clk->hal.ops = &sim_clock_ops;
	clk->hal.priv = NULL;
	clk->hal.name = "sim-ptp-clock";
	clk->hal.max_adjust_ppb = SIM_CLOCK_MAX_ADJUST_PPB;
	clk->hal.resolution_ns = 1;

	clk->source = source ? source : paad_sim_monotonic_ns;
	clk->source_ctx = source_ctx;
	clk->ppb = 0;
	clk->natural_ppb = natural_ppb;
	clk->ref_source_ns = clk->source(clk->source_ctx);
	clk->ref_clock_ns = initial_ns;

	if (pthread_mutex_init(&clk->lock, NULL) != 0)
		return PAAD_HAL_ERR_IO;

	return PAAD_HAL_OK;
}

void paad_sim_clock_deinit(struct paad_sim_clock *clk)
{
	pthread_mutex_destroy(&clk->lock);
}
