/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* Simulator locks, with the optional preemption hooks of paad/sim/hooks.h. */

#ifndef PAAD_SIM_LOCK_H
#define PAAD_SIM_LOCK_H

#include <pthread.h>
#include <stdlib.h>

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

/* The host allocator takes locks too: same treatment. */
static inline void *sim_malloc(size_t size)
{
	void *p;

	if (paad_sim_hooks.enter)
		paad_sim_hooks.enter();
	p = malloc(size);
	if (paad_sim_hooks.exit)
		paad_sim_hooks.exit();

	return p;
}

static inline void *sim_calloc(size_t n, size_t size)
{
	void *p;

	if (paad_sim_hooks.enter)
		paad_sim_hooks.enter();
	p = calloc(n, size);
	if (paad_sim_hooks.exit)
		paad_sim_hooks.exit();

	return p;
}

static inline void sim_free(void *p)
{
	if (paad_sim_hooks.enter)
		paad_sim_hooks.enter();
	free(p);
	if (paad_sim_hooks.exit)
		paad_sim_hooks.exit();
}

#endif /* PAAD_SIM_LOCK_H */
