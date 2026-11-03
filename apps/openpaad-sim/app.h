/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* openpaad-sim: helpers shared by its parts (main.c). */

#ifndef OPENPAAD_SIM_APP_H
#define OPENPAAD_SIM_APP_H

#include <stdint.h>

#include "paad/sim/world.h"

/* Sleep until a simulated time. FreeRTOS's tick can lose time on a busy
 * host, so waits are paced by the simulated clock, not by ticks. */
void app_sleep_until(struct paad_sim_world *world, uint64_t when);

#endif
