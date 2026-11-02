/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <time.h>

#include "paad/sim/world.h"

enum due_kind {
	DUE_NONE,
	DUE_TIMER,
	DUE_EVENT,
};

struct due {
	enum due_kind kind;
	uint64_t source_ns;
	uint64_t seq;
	struct paad_sim_timer *timer;
	struct paad_sim_event *event;
};

uint64_t paad_sim_world_now(void *ctx)
{
	struct paad_sim_world *world = ctx;

	if (world->mode == PAAD_SIM_REAL_TIME)
		return paad_sim_monotonic_ns(NULL);

	return __atomic_load_n(&world->virtual_now, __ATOMIC_ACQUIRE);
}

/* The earliest thing due, timers and events together. Timer deadlines are
 * recomputed from their clock every time, so a frequency change or a step
 * after arming is always honoured. Caller holds the lock. */
static struct due next_due(struct paad_sim_world *world)
{
	struct due best = { .kind = DUE_NONE, .source_ns = UINT64_MAX, .seq = UINT64_MAX };
	unsigned int i;

	for (i = 0; i < world->num_timers; i++) {
		struct paad_sim_timer *t = world->timers[i];
		uint64_t at;

		if (!t->armed)
			continue;

		at = paad_sim_clock_source_at(t->clock, t->expiry_ns);
		if (at < best.source_ns) {
			best.kind = DUE_TIMER;
			best.source_ns = at;
			best.seq = 0;
			best.timer = t;
			best.event = NULL;
		}
	}

	for (i = 0; i < PAAD_SIM_MAX_EVENTS; i++) {
		struct paad_sim_event *e = &world->events[i];

		if (!e->used)
			continue;

		if (e->source_ns < best.source_ns ||
		    (e->source_ns == best.source_ns && best.kind == DUE_EVENT && e->seq < best.seq)) {
			best.kind = DUE_EVENT;
			best.source_ns = e->source_ns;
			best.seq = e->seq;
			best.timer = NULL;
			best.event = e;
		}
	}

	return best;
}

/* Run everything due up to limit_ns. In virtual time, the clock advances to
 * each item as it runs. Callbacks run without the lock held. */
static void run_due(struct paad_sim_world *world, uint64_t limit_ns)
{
	for (;;) {
		paad_hal_timer_cb timer_cb = NULL;
		void *timer_arg = NULL;
		paad_sim_event_fn event_fn = NULL;
		void *event_arg = NULL;
		uint64_t at;
		struct due d;

		pthread_mutex_lock(&world->lock);
		d = next_due(world);
		if (d.kind == DUE_NONE || d.source_ns > limit_ns) {
			pthread_mutex_unlock(&world->lock);
			return;
		}

		at = d.source_ns;
		if (world->mode == PAAD_SIM_VIRTUAL_TIME && at > world->virtual_now)
			__atomic_store_n(&world->virtual_now, at, __ATOMIC_RELEASE);

		if (d.kind == DUE_TIMER) {
			struct paad_sim_timer *t = d.timer;

			t->fired++;
			if (t->period_ns)
				t->expiry_ns += t->period_ns;
			else
				t->armed = false;
			timer_cb = t->hal.callback;
			timer_arg = t->hal.callback_arg;
		} else {
			event_fn = d.event->fn;
			event_arg = d.event->arg;
			d.event->used = false;
		}
		pthread_mutex_unlock(&world->lock);

		if (timer_cb)
			timer_cb(timer_arg);
		if (event_fn)
			event_fn(event_arg, at);
	}
}

int paad_sim_world_run_until(struct paad_sim_world *world, uint64_t source_ns)
{
	if (world->mode != PAAD_SIM_VIRTUAL_TIME)
		return PAAD_HAL_ERR_STATE;

	run_due(world, source_ns);

	pthread_mutex_lock(&world->lock);
	if (source_ns > world->virtual_now)
		__atomic_store_n(&world->virtual_now, source_ns, __ATOMIC_RELEASE);
	pthread_mutex_unlock(&world->lock);

	return PAAD_HAL_OK;
}

int paad_sim_world_run_for(struct paad_sim_world *world, uint64_t duration_ns)
{
	return paad_sim_world_run_until(world, paad_sim_world_now(world) + duration_ns);
}

static void wake_thread(struct paad_sim_world *world)
{
	if (world->mode == PAAD_SIM_REAL_TIME)
		pthread_cond_signal(&world->wake);
}

int paad_sim_world_schedule(struct paad_sim_world *world, uint64_t source_ns, paad_sim_event_fn fn, void *arg)
{
	unsigned int i;

	pthread_mutex_lock(&world->lock);
	for (i = 0; i < PAAD_SIM_MAX_EVENTS; i++) {
		struct paad_sim_event *e = &world->events[i];

		if (e->used)
			continue;

		e->used = true;
		e->source_ns = source_ns;
		e->seq = world->next_seq++;
		e->fn = fn;
		e->arg = arg;
		wake_thread(world);
		pthread_mutex_unlock(&world->lock);
		return PAAD_HAL_OK;
	}
	world->events_dropped++;
	pthread_mutex_unlock(&world->lock);

	return PAAD_HAL_ERR_NO_MEMORY;
}

/* --- Real-time thread --------------------------------------------------- */

/* Sleep on the condition variable until a monotonic deadline, or until
 * woken. Caller holds the lock. */
static void wait_until(struct paad_sim_world *world, uint64_t deadline_ns)
{
#if defined(__APPLE__)
	/* macOS cannot bind a condition variable to the monotonic clock; use
	 * its relative wait instead. */
	uint64_t now = paad_sim_monotonic_ns(NULL);
	uint64_t rel = deadline_ns > now ? deadline_ns - now : 0;
	struct timespec ts = {
		.tv_sec = (time_t)(rel / PAAD_HAL_NSEC_PER_SEC),
		.tv_nsec = (long)(rel % PAAD_HAL_NSEC_PER_SEC),
	};

	pthread_cond_timedwait_relative_np(&world->wake, &world->lock, &ts);
#else
	struct timespec ts = {
		.tv_sec = (time_t)(deadline_ns / PAAD_HAL_NSEC_PER_SEC),
		.tv_nsec = (long)(deadline_ns % PAAD_HAL_NSEC_PER_SEC),
	};

	pthread_cond_timedwait(&world->wake, &world->lock, &ts);
#endif
}

static void *world_thread(void *arg)
{
	struct paad_sim_world *world = arg;

	pthread_mutex_lock(&world->lock);
	while (!world->stop_requested) {
		struct due d = next_due(world);
		uint64_t now = paad_sim_monotonic_ns(NULL);

		if (d.kind != DUE_NONE && d.source_ns <= now) {
			pthread_mutex_unlock(&world->lock);
			run_due(world, now);
			pthread_mutex_lock(&world->lock);
			continue;
		}

		if (d.kind == DUE_NONE)
			pthread_cond_wait(&world->wake, &world->lock);
		else
			wait_until(world, d.source_ns);
	}
	pthread_mutex_unlock(&world->lock);

	return NULL;
}

int paad_sim_world_start(struct paad_sim_world *world)
{
	if (world->mode != PAAD_SIM_REAL_TIME || world->thread_running)
		return PAAD_HAL_ERR_STATE;

	world->stop_requested = false;
	if (pthread_create(&world->thread, NULL, world_thread, world) != 0)
		return PAAD_HAL_ERR_IO;

	world->thread_running = true;

	return PAAD_HAL_OK;
}

void paad_sim_world_stop(struct paad_sim_world *world)
{
	if (!world->thread_running)
		return;

	pthread_mutex_lock(&world->lock);
	world->stop_requested = true;
	pthread_cond_signal(&world->wake);
	pthread_mutex_unlock(&world->lock);

	pthread_join(world->thread, NULL);
	world->thread_running = false;
}

int paad_sim_world_init(struct paad_sim_world *world, enum paad_sim_time_mode mode)
{
	pthread_condattr_t attr;

	memset(world, 0, sizeof(*world));
	world->mode = mode;
	world->virtual_now = 0;

	if (pthread_mutex_init(&world->lock, NULL) != 0)
		return PAAD_HAL_ERR_IO;

	/* The real-time thread waits on the monotonic clock, like source time
	 * (macOS: relative waits instead, see wait_until()). */
	pthread_condattr_init(&attr);
#if !defined(__APPLE__)
	pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
	if (pthread_cond_init(&world->wake, &attr) != 0) {
		pthread_condattr_destroy(&attr);
		pthread_mutex_destroy(&world->lock);
		return PAAD_HAL_ERR_IO;
	}
	pthread_condattr_destroy(&attr);

	return PAAD_HAL_OK;
}

void paad_sim_world_deinit(struct paad_sim_world *world)
{
	paad_sim_world_stop(world);
	pthread_cond_destroy(&world->wake);
	pthread_mutex_destroy(&world->lock);
}

/* --- HAL timers ----------------------------------------------------------- */

static int sim_timer_arm(struct paad_hal_timer *hal, uint64_t expiry_ns, uint64_t period_ns)
{
	struct paad_sim_timer *t = (struct paad_sim_timer *)hal;

	pthread_mutex_lock(&t->world->lock);
	t->expiry_ns = expiry_ns;
	t->period_ns = period_ns;
	t->armed = true;
	wake_thread(t->world);
	pthread_mutex_unlock(&t->world->lock);

	return PAAD_HAL_OK;
}

static int sim_timer_cancel(struct paad_hal_timer *hal)
{
	struct paad_sim_timer *t = (struct paad_sim_timer *)hal;

	pthread_mutex_lock(&t->world->lock);
	t->armed = false;
	pthread_mutex_unlock(&t->world->lock);

	return PAAD_HAL_OK;
}

static const struct paad_hal_timer_ops sim_timer_ops = {
	.arm = sim_timer_arm,
	.cancel = sim_timer_cancel,
	.set_output = NULL,
};

int paad_sim_timer_init(struct paad_sim_timer *timer, struct paad_sim_world *world, struct paad_sim_clock *clock)
{
	int rc = PAAD_HAL_OK;

	memset(timer, 0, sizeof(*timer));
	timer->hal.ops = &sim_timer_ops;
	timer->hal.name = "sim-timer";
	timer->hal.clock = &clock->hal;
	timer->hal.resolution_ns = 1;
	timer->world = world;
	timer->clock = clock;

	pthread_mutex_lock(&world->lock);
	if (world->num_timers < PAAD_SIM_MAX_TIMERS)
		world->timers[world->num_timers++] = timer;
	else
		rc = PAAD_HAL_ERR_NO_MEMORY;
	pthread_mutex_unlock(&world->lock);

	return rc;
}

void paad_sim_timer_deinit(struct paad_sim_timer *timer)
{
	struct paad_sim_world *world = timer->world;
	unsigned int i;

	pthread_mutex_lock(&world->lock);
	for (i = 0; i < world->num_timers; i++) {
		if (world->timers[i] == timer) {
			world->timers[i] = world->timers[--world->num_timers];
			break;
		}
	}
	timer->armed = false;
	pthread_mutex_unlock(&world->lock);
}
