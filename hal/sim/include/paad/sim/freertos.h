/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * Simulated interrupts for FreeRTOS on the host.
 *
 * On hardware, timer expiries and network events arrive as interrupts. In
 * the simulator they arrive from a FreeRTOS task of the highest priority
 * that runs the simulation world: its callbacks then run in FreeRTOS
 * context, can use the kernel's *FromISR() services like interrupt handlers
 * do, and preempt every other task. Resolution is the FreeRTOS tick.
 */

#ifndef PAAD_SIM_FREERTOS_H
#define PAAD_SIM_FREERTOS_H

#include "paad/sim/world.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Install the simulator's preemption hooks and start the interrupt task
 * for this world. Call from a FreeRTOS task, before the HAL is used. */
int paad_sim_freertos_start(struct paad_sim_world *world);

#ifdef __cplusplus
}
#endif

#endif /* PAAD_SIM_FREERTOS_H */
