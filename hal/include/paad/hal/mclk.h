/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * HAL service: media clock.
 *
 * The device's audio clock (the master clock of its converters), steerable
 * in frequency, with hardware captures that relate it to gPTP time.
 *
 * Media clock recovery compares the audio clock with timestamps carried by
 * the network (CRF or AAF) and steers it until both run at the same rate and
 * phase. It needs two things from the platform, both here: a fine frequency
 * adjustment (typically a fractional PLL), and captures of a counter driven
 * by the audio clock taken at known gPTP instants.
 */

#ifndef PAAD_HAL_MCLK_H
#define PAAD_HAL_MCLK_H

#include "paad/hal/common.h"
#include "paad/hal/clock.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One capture: the media clock counter value at a gPTP instant. */
struct paad_hal_mclk_sample {
	uint64_t ptp_ns;     /**< gPTP time of the capture */
	uint64_t mclk_ticks; /**< media clock counter at that time (wraps are unfolded by the port) */
};

struct paad_hal_mclk;

/** May run in interrupt context. */
typedef void (*paad_hal_mclk_capture_cb)(struct paad_hal_mclk *mclk, const struct paad_hal_mclk_sample *sample, void *arg);

struct paad_hal_mclk_ops {
	/** Set the audio clock's frequency offset from nominal, in ppb
	 * (absolute). The platform applies the nearest value it can and
	 * reports it in *applied (may be NULL). ISR-safe. Required. */
	int (*adjust)(struct paad_hal_mclk *mclk, paad_hal_ppb_t ppb, paad_hal_ppb_t *applied);

	/** Capture the media clock counter when gPTP time reaches ptp_ns; the
	 * result arrives through capture_cb. Only one capture is pending at a
	 * time. ISR-safe. Required. */
	int (*request_capture)(struct paad_hal_mclk *mclk, uint64_t ptp_ns);
};

struct paad_hal_mclk {
	const struct paad_hal_mclk_ops *ops;
	void *priv;
	const char *name;
	struct paad_hal_clock *ptp_clock; /**< gPTP clock the captures refer to */
	uint32_t tick_hz;                 /**< nominal rate of mclk_ticks, e.g. 24576000 */
	uint32_t sample_rate;             /**< nominal audio sample rate, e.g. 48000 */
	paad_hal_ppb_t max_adjust_ppb;
	paad_hal_mclk_capture_cb capture_cb; /**< set by the core */
	void *capture_arg;
};

static inline int paad_hal_mclk_adjust(struct paad_hal_mclk *mclk, paad_hal_ppb_t ppb, paad_hal_ppb_t *applied)
{
	return mclk->ops->adjust(mclk, ppb, applied);
}

static inline int paad_hal_mclk_request_capture(struct paad_hal_mclk *mclk, uint64_t ptp_ns)
{
	return mclk->ops->request_capture(mclk, ptp_ns);
}

#ifdef __cplusplus
}
#endif

#endif /* PAAD_HAL_MCLK_H */
