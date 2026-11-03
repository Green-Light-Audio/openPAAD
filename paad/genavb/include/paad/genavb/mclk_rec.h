/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * GenAVB media clock recovery on the openPAAD HAL media clock.
 *
 * The first HAL network port registers it with GenAVB when the platform has
 * a media clock (paad_hal_platform.mclk). GenAVB then steers that clock:
 * to gPTP when a clock domain's source is the PTP clock (a talker), or to a
 * stream's timestamps when the source is an input stream (a listener). The
 * algorithm is GenAVB's own (common/os/media_clock_rec_pll_common.c, used
 * unchanged); this driver gives it the HAL's adjustment and captures.
 */

#ifndef PAAD_GENAVB_MCLK_REC_H
#define PAAD_GENAVB_MCLK_REC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum paad_genavb_mclk_rec_state {
	PAAD_GENAVB_MCLK_REC_STOPPED,
	PAAD_GENAVB_MCLK_REC_STARTING,  /**< first captures */
	PAAD_GENAVB_MCLK_REC_MEASURING, /**< estimating the frequency error */
	PAAD_GENAVB_MCLK_REC_ADJUSTING, /**< steering, not yet locked */
	PAAD_GENAVB_MCLK_REC_LOCKED,    /**< steering, locked */
};

struct paad_genavb_mclk_rec_status {
	bool registered;      /**< the platform has a media clock and GenAVB knows it */
	bool from_stream;     /**< following a stream (else gPTP) */
	enum paad_genavb_mclk_rec_state state;
	int32_t adjust_ppb;   /**< applied to the media clock */
	int32_t phase_err_ticks; /**< phase error measured by GenAVB, once per second */
	uint32_t captures;    /**< captures used */
	uint32_t resets;      /**< restarts of the recovery after an error */
	uint32_t err_meas;    /**< captures rejected as out of range */
	uint32_t err_ts;      /**< no stream timestamp when one was needed */
	uint32_t err_wd;      /**< captures that never came */
	uint32_t err_capture; /**< capture requests the HAL refused */
};

/** Snapshot of the recovery; registered is false if there is none. */
void paad_genavb_mclk_rec_status(struct paad_genavb_mclk_rec_status *status);

#ifdef __cplusplus
}
#endif

#endif /* PAAD_GENAVB_MCLK_REC_H */
