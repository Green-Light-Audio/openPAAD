/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * Running the simulated hardware under an RTOS on the host.
 *
 * With FreeRTOS on its POSIX port, tasks are host threads that the kernel
 * suspends and resumes with signals. A task suspended while holding one of
 * the simulator's internal locks would block every task that needs the
 * same lock, so the simulator brackets each locked section with hooks that
 * disable preemption (taskENTER_CRITICAL() / taskEXIT_CRITICAL()). Without
 * hooks (plain host threads, tests) the locks are ordinary mutexes.
 *
 * Set the hooks before any simulator object is used, and do not combine
 * them with the world's own background thread (paad_sim_world_start()):
 * run the world from an RTOS task instead (paad_sim_world_run_pending()).
 */

#ifndef PAAD_SIM_HOOKS_H
#define PAAD_SIM_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

void paad_sim_set_critical_hooks(void (*enter)(void), void (*exit)(void));

#ifdef __cplusplus
}
#endif

#endif /* PAAD_SIM_HOOKS_H */
