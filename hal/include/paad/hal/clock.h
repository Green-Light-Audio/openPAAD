/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * HAL service: gPTP hardware clock.
 *
 * The clock that timestamps network frames and that gPTP disciplines. On an
 * MCU it is usually the IEEE 1588 timer of the Ethernet MAC; on a host it is
 * a simulated or PHC clock. It counts nanoseconds and never runs backwards
 * except through paad_hal_clock_step().
 */

#ifndef PAAD_HAL_CLOCK_H
#define PAAD_HAL_CLOCK_H

#include "paad/hal/common.h"

#ifdef __cplusplus
extern "C" {
#endif

struct paad_hal_clock;

struct paad_hal_clock_ops {
	/** Current time in ns. ISR-safe. Required. */
	uint64_t (*read_ns)(struct paad_hal_clock *clk);

	/** Set the frequency offset from nominal, in ppb (absolute, not
	 * cumulative). Values beyond max_adjust_ppb are clamped. Required. */
	int (*adjust_frequency)(struct paad_hal_clock *clk, paad_hal_ppb_t ppb);

	/** Add offset_ns to the current time (step). Required. */
	int (*step)(struct paad_hal_clock *clk, int64_t offset_ns);
};

struct paad_hal_clock {
	const struct paad_hal_clock_ops *ops;
	void *priv;
	const char *name;
	paad_hal_ppb_t max_adjust_ppb; /**< largest frequency offset accepted */
	uint32_t resolution_ns;        /**< smallest step of read_ns() */
};

static inline uint64_t paad_hal_clock_read_ns(struct paad_hal_clock *clk)
{
	return clk->ops->read_ns(clk);
}

static inline int paad_hal_clock_adjust_frequency(struct paad_hal_clock *clk, paad_hal_ppb_t ppb)
{
	return clk->ops->adjust_frequency(clk, ppb);
}

static inline int paad_hal_clock_step(struct paad_hal_clock *clk, int64_t offset_ns)
{
	return clk->ops->step(clk, offset_ns);
}

#ifdef __cplusplus
}
#endif

#endif /* PAAD_HAL_CLOCK_H */
