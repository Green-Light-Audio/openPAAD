/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * Simulation world: the time line shared by every simulated endpoint.
 *
 * The world owns "source time" (the reference all simulated clocks derive
 * from) and runs everything that happens at a given source time: timer
 * expiries and frame deliveries, in time order.
 *
 * Two modes:
 *  - virtual time: time only moves when the caller runs the world forward
 *    (paad_sim_world_run_until). Deterministic and instantaneous; used by
 *    tests and CI.
 *  - real time: source time is the host's monotonic clock and a background
 *    thread runs events as they fall due; used by openpaad-sim so that
 *    Hive and other real controllers can talk to simulated endpoints.
 */

#ifndef PAAD_SIM_WORLD_H
#define PAAD_SIM_WORLD_H

#include <pthread.h>

#include "paad/hal/timer.h"
#include "paad/sim/clock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAAD_SIM_MAX_TIMERS 64
#define PAAD_SIM_MAX_EVENTS 1024

enum paad_sim_time_mode {
	PAAD_SIM_VIRTUAL_TIME,
	PAAD_SIM_REAL_TIME,
};

typedef void (*paad_sim_event_fn)(void *arg, uint64_t source_ns);

struct paad_sim_event {
	bool used;
	uint64_t source_ns;
	uint64_t seq; /* keeps events at the same time in scheduling order */
	paad_sim_event_fn fn;
	void *arg;
};

struct paad_sim_timer;

struct paad_sim_world {
	enum paad_sim_time_mode mode;
	uint64_t virtual_now;
	uint64_t next_seq;

	pthread_mutex_t lock;
	pthread_cond_t wake;
	pthread_t thread;
	bool thread_running;
	bool stop_requested;

	struct paad_sim_timer *timers[PAAD_SIM_MAX_TIMERS];
	unsigned int num_timers;

	struct paad_sim_event events[PAAD_SIM_MAX_EVENTS];
	unsigned int events_dropped;

	void (*wake_hook)(void *arg);
	void *wake_arg;
};

/** HAL timer on a simulated clock. */
struct paad_sim_timer {
	struct paad_hal_timer hal; /**< the HAL object, first member */
	struct paad_sim_world *world;
	struct paad_sim_clock *clock;
	bool armed;
	uint64_t expiry_ns; /**< on the timer's clock */
	uint64_t period_ns;
	unsigned int fired;
};

int paad_sim_world_init(struct paad_sim_world *world, enum paad_sim_time_mode mode);
void paad_sim_world_deinit(struct paad_sim_world *world);

/** Current source time. Usable as a paad_sim_time_source (ctx = world). */
uint64_t paad_sim_world_now(void *world);

/** Virtual time: run every event due up to source_ns, then set the time to
 * source_ns. Callbacks run in the caller's thread. */
int paad_sim_world_run_until(struct paad_sim_world *world, uint64_t source_ns);
int paad_sim_world_run_for(struct paad_sim_world *world, uint64_t duration_ns);

/** Run every event due now, in the caller's thread, and return the source
 * time of the next one (UINT64_MAX if none). For running the world from an
 * RTOS task instead of the background thread; wait until the returned time,
 * or until the wake hook fires, then call again. Any mode. */
uint64_t paad_sim_world_run_pending(struct paad_sim_world *world);

/** fn(arg) is called whenever an event or a timer is added, from the
 * caller's context and without simulator locks held. */
void paad_sim_world_set_wake_hook(struct paad_sim_world *world, void (*fn)(void *arg), void *arg);

/** Real time: start or stop the background thread that runs events. */
int paad_sim_world_start(struct paad_sim_world *world);
void paad_sim_world_stop(struct paad_sim_world *world);

/** Call fn(arg) at source time source_ns. For simulated hardware, not for
 * application code. Returns PAAD_HAL_ERR_NO_MEMORY if the queue is full. */
int paad_sim_world_schedule(struct paad_sim_world *world, uint64_t source_ns, paad_sim_event_fn fn, void *arg);

/** Initialise a HAL timer on a simulated clock and register it. */
int paad_sim_timer_init(struct paad_sim_timer *timer, struct paad_sim_world *world, struct paad_sim_clock *clock);
void paad_sim_timer_deinit(struct paad_sim_timer *timer);

#ifdef __cplusplus
}
#endif

#endif /* PAAD_SIM_WORLD_H */
