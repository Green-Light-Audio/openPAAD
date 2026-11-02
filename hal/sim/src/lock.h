/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* Simulator locks, with the optional preemption hooks of paad/sim/hooks.h. */

#ifndef PAAD_SIM_LOCK_H
#define PAAD_SIM_LOCK_H

#include <pthread.h>

#include "paad/sim/hooks.h"

struct paad_sim_hooks {
	void (*enter)(void);
	void (*exit)(void);
};

extern struct paad_sim_hooks paad_sim_hooks;

static inline void sim_lock(pthread_mutex_t *m)
{
	if (paad_sim_hooks.enter)
		paad_sim_hooks.enter();
	pthread_mutex_lock(m);
}

static inline void sim_unlock(pthread_mutex_t *m)
{
	pthread_mutex_unlock(m);
	if (paad_sim_hooks.exit)
		paad_sim_hooks.exit();
}

#endif /* PAAD_SIM_LOCK_H */
