/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * Hooks of the FreeRTOS host build (src/freertos_hooks.c): what the idle
 * task does, and the gate the POSIX port's tick thread waits on in virtual
 * time. Used by paad_sim_freertos_start().
 */

#ifndef PAAD_SIM_FREERTOS_HOOKS_H
#define PAAD_SIM_FREERTOS_HOOKS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Run fn from the idle task instead of sleeping until the next tick. */
void paad_sim_freertos_set_idle(void (*fn)(void));

/** Gate the tick thread: each tick then waits for a release. */
void paad_sim_freertos_gate_ticks(bool on);
void paad_sim_freertos_release_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* PAAD_SIM_FREERTOS_HOOKS_H */
