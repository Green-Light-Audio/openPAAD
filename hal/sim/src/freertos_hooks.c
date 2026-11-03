/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FreeRTOS application hooks for the host (POSIX port), and the tick gate
 * used by virtual time. */

#include <pthread.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"

#include "paad/sim/freertos_hooks.h"

static void (*idle_fn)(void);

static struct {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	unsigned int tokens;
	bool gated;
} tick_gate = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, false };

void paad_sim_freertos_set_idle(void (*fn)(void))
{
	idle_fn = fn;
}

void paad_sim_freertos_gate_ticks(bool on)
{
	pthread_mutex_lock(&tick_gate.lock);
	tick_gate.gated = on;
	pthread_cond_broadcast(&tick_gate.cond);
	pthread_mutex_unlock(&tick_gate.lock);
}

void paad_sim_freertos_release_tick(void)
{
	pthread_mutex_lock(&tick_gate.lock);
	tick_gate.tokens++;
	pthread_cond_signal(&tick_gate.cond);
	pthread_mutex_unlock(&tick_gate.lock);
}

/* The POSIX port's tick thread raises a tick, then sleeps for one tick
 * period; port.c is built with usleep() renamed to this. In virtual time
 * the "sleep" lasts until the simulation decides the next tick is due. */
int paad_sim_port_usleep(useconds_t us);

int paad_sim_port_usleep(useconds_t us)
{
	pthread_mutex_lock(&tick_gate.lock);
	if (!tick_gate.gated) {
		pthread_mutex_unlock(&tick_gate.lock);
		return usleep(us);
	}
	while (tick_gate.gated && !tick_gate.tokens)
		pthread_cond_wait(&tick_gate.cond, &tick_gate.lock);
	if (tick_gate.tokens)
		tick_gate.tokens--;
	pthread_mutex_unlock(&tick_gate.lock);

	return 0;
}

/* With nothing to run, the idle task would spin and keep a host core busy:
 * in real time it sleeps (the tick signal interrupts the sleep); in virtual
 * time it moves the simulation forward instead. */
void vApplicationIdleHook(void)
{
	const struct timespec ts = { 0, 1000000000L / configTICK_RATE_HZ };

	if (idle_fn)
		idle_fn();
	else
		nanosleep(&ts, NULL);
}
