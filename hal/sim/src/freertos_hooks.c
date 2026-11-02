/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FreeRTOS application hooks for the host (POSIX port). */

#include <time.h>

#include "FreeRTOS.h"
#include "task.h"

/* With nothing to run, the idle task would spin and keep a host core busy.
 * Sleep instead: the tick signal interrupts the sleep, so nothing is
 * delayed beyond the tick resolution the simulation already has. */
void vApplicationIdleHook(void)
{
	const struct timespec ts = { 0, 1000000000L / configTICK_RATE_HZ };

	nanosleep(&ts, NULL);
}
