/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>

#include "paad/sim/mclk.h"

#include "lock.h"

/* Counter value, in 1/2^32 tick, at a source time, at the current rate.
 * Caller holds the lock. The effective rate is tick_hz x (1 + (natural error
 * + adjustment) / 10^9); 128-bit arithmetic keeps the result exact to the
 * fraction for any realistic time span. */
static __int128 count_q32_at(const struct paad_sim_mclk *m, uint64_t source_ns)
{
	const __int128 den = (__int128)PAAD_HAL_NSEC_PER_SEC * (__int128)PAAD_HAL_NSEC_PER_SEC;
	const __int128 rate = (__int128)m->hal.tick_hz *
			      ((int64_t)PAAD_HAL_NSEC_PER_SEC + (int64_t)m->natural_ppb + (int64_t)m->ppb);
	const __int128 base = ((__int128)m->ref.ticks << 32) | m->ref.frac;
	const bool before = source_ns < m->ref_source_ns;
	const uint64_t d = before ? m->ref_source_ns - source_ns : source_ns - m->ref_source_ns;
	const __int128 num = (__int128)d * rate;
	const __int128 delta = ((num / den) << 32) + ((num % den) << 32) / den;
	const __int128 count = before ? base - delta : base + delta;

	return count < 0 ? 0 : count;
}

static struct paad_sim_mclk_count to_count(__int128 q32)
{
	struct paad_sim_mclk_count c = {
		.ticks = (uint64_t)(q32 >> 32),
		.frac = (uint32_t)q32,
	};

	return c;
}

/* Move the reference point to now, so that a rate change applies from now
 * on and the count stays continuous. Caller holds the lock. */
static void rebase(struct paad_sim_mclk *m)
{
	uint64_t now = paad_sim_world_now(m->world);

	m->ref = to_count(count_q32_at(m, now));
	m->ref_source_ns = now;
}

struct paad_sim_mclk_count paad_sim_mclk_count_at(struct paad_sim_mclk *mclk, uint64_t source_ns)
{
	struct paad_sim_mclk_count c;

	sim_lock(&mclk->lock);
	c = to_count(count_q32_at(mclk, source_ns));
	sim_unlock(&mclk->lock);

	return c;
}

paad_hal_ppb_t paad_sim_mclk_adjustment(struct paad_sim_mclk *mclk)
{
	paad_hal_ppb_t ppb;

	sim_lock(&mclk->lock);
	ppb = mclk->ppb;
	sim_unlock(&mclk->lock);

	return ppb;
}

static int sim_mclk_adjust(struct paad_hal_mclk *hal, paad_hal_ppb_t ppb, paad_hal_ppb_t *applied)
{
	struct paad_sim_mclk *m = (struct paad_sim_mclk *)hal;

	if (ppb > hal->max_adjust_ppb)
		ppb = hal->max_adjust_ppb;
	else if (ppb < -hal->max_adjust_ppb)
		ppb = -hal->max_adjust_ppb;

	sim_lock(&m->lock);
	rebase(m);
	m->ppb = ppb;
	m->adjusts++;
	sim_unlock(&m->lock);

	if (applied)
		*applied = ppb;

	return PAAD_HAL_OK;
}

/* The capture instant has come, as far as the world knows: latch the
 * counter at the exact source time the gPTP clock reached it. */
static void capture_event(void *arg, uint64_t source_ns)
{
	struct paad_sim_mclk *m = arg;
	struct paad_hal_mclk_sample sample;
	paad_hal_mclk_capture_cb cb;
	void *cb_arg;
	uint64_t now = paad_sim_world_now(m->world);
	uint64_t at;

	(void)source_ns;

	sim_lock(&m->lock);
	if (!m->capture_pending) {
		sim_unlock(&m->lock);
		return;
	}

	at = paad_sim_clock_source_at(m->clock, m->capture_ns);
	if (at > now) {
		/* The gPTP clock was slowed down after the request: not yet. */
		sim_unlock(&m->lock);
		if (paad_sim_world_schedule(m->world, at, capture_event, m) != PAAD_HAL_OK) {
			sim_lock(&m->lock);
			m->capture_pending = false; /* lost, like a missed hardware event */
			sim_unlock(&m->lock);
		}
		return;
	}

	sample.ptp_ns = m->capture_ns;
	sample.mclk_ticks = to_count(count_q32_at(m, at)).ticks;
	m->capture_pending = false;
	m->captures++;
	cb = m->hal.capture_cb;
	cb_arg = m->hal.capture_arg;
	sim_unlock(&m->lock);

	if (cb)
		cb(&m->hal, &sample, cb_arg);
}

static int sim_mclk_request_capture(struct paad_hal_mclk *hal, uint64_t ptp_ns)
{
	struct paad_sim_mclk *m = (struct paad_sim_mclk *)hal;
	uint64_t now = paad_sim_world_now(m->world);
	int rc;

	sim_lock(&m->lock);
	if (m->capture_pending) {
		sim_unlock(&m->lock);
		return PAAD_HAL_ERR_BUSY;
	}
	if (paad_sim_clock_at(m->clock, now) >= ptp_ns) {
		sim_unlock(&m->lock);
		return PAAD_HAL_ERR_INVALID; /* already passed */
	}
	m->capture_pending = true;
	m->capture_ns = ptp_ns;
	sim_unlock(&m->lock);

	rc = paad_sim_world_schedule(m->world, paad_sim_clock_source_at(m->clock, ptp_ns), capture_event, m);
	if (rc != PAAD_HAL_OK) {
		sim_lock(&m->lock);
		m->capture_pending = false;
		sim_unlock(&m->lock);
	}

	return rc;
}

static const struct paad_hal_mclk_ops sim_mclk_ops = {
	.adjust = sim_mclk_adjust,
	.request_capture = sim_mclk_request_capture,
};

int paad_sim_mclk_init(struct paad_sim_mclk *mclk, struct paad_sim_world *world, struct paad_sim_clock *clock,
		       uint32_t tick_hz, uint32_t sample_rate, paad_hal_ppb_t natural_ppb)
{
	if (!mclk || !world || !clock || !tick_hz || !sample_rate)
		return PAAD_HAL_ERR_INVALID;

	memset(mclk, 0, sizeof(*mclk));
	mclk->hal.ops = &sim_mclk_ops;
	mclk->hal.name = "sim-mclk";
	mclk->hal.ptp_clock = &clock->hal;
	mclk->hal.tick_hz = tick_hz;
	mclk->hal.sample_rate = sample_rate;
	mclk->hal.max_adjust_ppb = PAAD_SIM_MCLK_MAX_ADJUST_PPB;
	mclk->world = world;
	mclk->clock = clock;
	mclk->natural_ppb = natural_ppb;
	mclk->ref_source_ns = paad_sim_world_now(world);

	if (pthread_mutex_init(&mclk->lock, NULL) != 0)
		return PAAD_HAL_ERR_IO;

	return PAAD_HAL_OK;
}

void paad_sim_mclk_deinit(struct paad_sim_mclk *mclk)
{
	pthread_mutex_destroy(&mclk->lock);
}
