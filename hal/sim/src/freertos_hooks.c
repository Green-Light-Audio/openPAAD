/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FreeRTOS application hooks for the host (POSIX port), and the tick gate
 * used by virtual time. */

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
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

/* Real time: the tick thread sleeps one period between ticks, whatever time
 * the tick itself took, and a busy host makes it oversleep. FreeRTOS time
 * then falls behind the host's, by 15 % on an idle Linux machine and more on
 * a loaded one, and so does everything timed in ticks: GenAVB's system
 * timers, such as ATDECC's advertising delays. Put the missing ticks back,
 * so that FreeRTOS time follows the host's clock. */
#define CATCH_UP_MAX 100 /* ticks per call, to keep each catch-up short */

static void catch_up_ticks(void)
{
	static bool started;
	static uint64_t t0_ns;
	static TickType_t tick0;
	struct timespec ts;
	uint64_t now_ns;
	TickType_t expected;
	int32_t behind;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;

	if (!started) {
		started = true;
		t0_ns = now_ns;
		tick0 = xTaskGetTickCount();
		return;
	}

	expected = (TickType_t)(tick0 + (now_ns - t0_ns) * configTICK_RATE_HZ / 1000000000ULL);
	behind = (int32_t)(expected - xTaskGetTickCount());
	if (behind > 1)
		xTaskCatchUpTicks((TickType_t)(behind > CATCH_UP_MAX ? CATCH_UP_MAX : behind));
}

/* With nothing to run, the idle task would spin and keep a host core busy:
 * in real time it sleeps (the tick signal interrupts the sleep); in virtual
 * time it moves the simulation forward instead. */
void vApplicationIdleHook(void)
{
	const struct timespec ts = { 0, 1000000000L / configTICK_RATE_HZ };

	if (idle_fn) {
		idle_fn();
	} else {
		catch_up_ticks();
		nanosleep(&ts, NULL);
	}
}
