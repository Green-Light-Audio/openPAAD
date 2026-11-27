/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
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
 * do, and preempt every other task.
 *
 * Real time: the world follows the host's clock; resolution is the
 * FreeRTOS tick. Virtual time (world in PAAD_SIM_VIRTUAL_TIME): time stands
 * still while any task has work; once all are idle, the idle task moves it
 * to the next simulated event or FreeRTOS tick, whichever comes first, so
 * every timer fires exactly on time whatever the host's speed or load
 * (decision 0013).
 */

#ifndef PAAD_SIM_FREERTOS_H
#define PAAD_SIM_FREERTOS_H

#include "paad/sim/freertos_hooks.h"
#include "paad/sim/world.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Install the simulator's preemption hooks and start the interrupt task
 * for this world. Call from a FreeRTOS task, before the HAL is used. */
int paad_sim_freertos_start(struct paad_sim_world *world);

/** Virtual time: keep this world in lockstep with the device at the other
 * end of a cable to another process (paad_sim_eth_connect_socket). Call
 * before paad_sim_freertos_start(). */
struct paad_sim_eth;
void paad_sim_freertos_lockstep(struct paad_sim_eth *eth);


#ifdef __cplusplus
}
#endif

#endif /* PAAD_SIM_FREERTOS_H */
