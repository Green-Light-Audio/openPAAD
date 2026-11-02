/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * HAL service: event timers on the gPTP clock.
 *
 * A timer fires a callback when the gPTP clock reaches a given time, once or
 * periodically. The stack uses them to schedule gPTP messages and AVTP
 * transmissions. On an MCU they are compare channels of the 1588 timer or a
 * general-purpose timer kept in step with it.
 */

#ifndef PAAD_HAL_TIMER_H
#define PAAD_HAL_TIMER_H

#include "paad/hal/common.h"
#include "paad/hal/clock.h"

#ifdef __cplusplus
extern "C" {
#endif

struct paad_hal_timer;

typedef void (*paad_hal_timer_cb)(void *arg);

struct paad_hal_timer_ops {
	/** Fire at expiry_ns (gPTP time), then every period_ns if period_ns is
	 * not 0. Re-arming replaces the previous setting. An expiry already in
	 * the past fires as soon as possible. ISR-safe. Required. */
	int (*arm)(struct paad_hal_timer *timer, uint64_t expiry_ns, uint64_t period_ns);

	/** Stop the timer; no callback runs after this returns. ISR-safe.
	 * Required. */
	int (*cancel)(struct paad_hal_timer *timer);

	/** Optional: drive a hardware output pin on each expiry (for example a
	 * pulse per second for measurements). */
	int (*set_output)(struct paad_hal_timer *timer, bool enable);
};

struct paad_hal_timer {
	const struct paad_hal_timer_ops *ops;
	void *priv;
	const char *name;
	struct paad_hal_clock *clock; /**< clock the expiry times refer to */
	uint32_t resolution_ns;
	paad_hal_timer_cb callback;   /**< set by the core before arming */
	void *callback_arg;
};

static inline int paad_hal_timer_arm(struct paad_hal_timer *timer, uint64_t expiry_ns, uint64_t period_ns)
{
	return timer->ops->arm(timer, expiry_ns, period_ns);
}

static inline int paad_hal_timer_cancel(struct paad_hal_timer *timer)
{
	return timer->ops->cancel(timer);
}

static inline int paad_hal_timer_set_output(struct paad_hal_timer *timer, bool enable)
{
	if (!timer->ops->set_output)
		return PAAD_HAL_ERR_NOT_SUPPORTED;

	return timer->ops->set_output(timer, enable);
}

#ifdef __cplusplus
}
#endif

#endif /* PAAD_HAL_TIMER_H */
