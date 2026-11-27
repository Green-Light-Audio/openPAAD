/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sched.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "paad/hal/common.h"
#include "paad/sim/eth.h"
#include "paad/sim/freertos.h"
#include "paad/sim/hooks.h"

#define IRQ_TASK_STACK      (configMINIMAL_STACK_SIZE * 4)
#define TICK_NS             (1000000000ULL / configTICK_RATE_HZ)
#define PEER_TIMEOUT_MS     10000

static TaskHandle_t irq_task_handle;
static struct paad_sim_world *sim_world;
static struct paad_sim_eth *lockstep_eth;
static uint64_t tick_base; /* simulated time of FreeRTOS tick 0 */

static void critical_enter(void)
{
	taskENTER_CRITICAL();
}

static void critical_exit(void)
{
	taskEXIT_CRITICAL();
}

/* May be called from the interrupt task itself or from any other task,
 * possibly inside a critical section: use the ISR-safe notification and
 * let the scheduler switch at the next opportunity. */
static void wake_irq_task(void *arg)
{
	BaseType_t woken = pdFALSE;

	(void)arg;

	if (irq_task_handle)
		vTaskNotifyGiveFromISR(irq_task_handle, &woken);
}

/* Real time: run what is due, then sleep until the next deadline. On a host
 * too busy to keep up, something is always due again by the time the rest
 * is done, and this task, the highest priority, would run for ever and
 * starve every other one. After MAX_BUSY_NS without a sleep, let the tasks
 * have a tick: interrupts come late then, as they do on a loaded host
 * anyway, and GenAVB's timers account for the periods they missed. */
#define MAX_BUSY_NS (2 * TICK_NS)

static void irq_task_real_time(struct paad_sim_world *world)
{
	uint64_t busy_since = paad_sim_world_now(world);

	for (;;) {
		uint64_t next = paad_sim_world_run_pending(world);
		uint64_t now = paad_sim_world_now(world);
		TickType_t ticks;

		if (next <= now) {
			if (now - busy_since < MAX_BUSY_NS)
				continue;
			vTaskDelay(1);
			busy_since = paad_sim_world_now(world);
			continue;
		}

		if (next == UINT64_MAX)
			ticks = portMAX_DELAY;
		else
			ticks = (TickType_t)((next - now + TICK_NS - 1) / TICK_NS);

		/* Pollers (cables to other processes) need a look every tick. */
		if (paad_sim_world_has_pollers(world) && ticks > 1)
			ticks = 1;

		ulTaskNotifyTake(pdTRUE, ticks);
		busy_since = paad_sim_world_now(world);
	}
}

static void irq_task(void *arg)
{
	struct paad_sim_world *world = arg;

	if (world->mode == PAAD_SIM_REAL_TIME)
		irq_task_real_time(world);

	/* Virtual time: woken by the idle task when an event is due. */
	for (;;) {
		paad_sim_world_run_pending(world);
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	}
}

/* Virtual time, from the idle task: every task is waiting, so move the
 * simulation to the next thing that happens. */
static void idle_advance(void)
{
	for (;;) {
		TickType_t ticks = xTaskGetTickCount();
		uint64_t tick_at = tick_base + ((uint64_t)ticks + 1) * TICK_NS;
		uint64_t event_at = paad_sim_world_next_event(sim_world);
		uint64_t next = event_at < tick_at ? event_at : tick_at;

		if (lockstep_eth) {
			paad_sim_eth_lockstep_announce(lockstep_eth, next);
			if (next > paad_sim_eth_lockstep_horizon(lockstep_eth)) {
				/* the other device may still send something
				 * that lands before then */
				paad_sim_eth_lockstep_wait(lockstep_eth, PEER_TIMEOUT_MS);
				continue;
			}
		}

		if (event_at <= tick_at) {
			paad_sim_world_advance(sim_world, event_at);
			xTaskNotifyGive(irq_task_handle);
			return;
		}

		/* A FreeRTOS tick: let the port's tick thread raise it, and wait
		 * until it has been taken. */
		paad_sim_world_advance(sim_world, tick_at);
		paad_sim_freertos_release_tick();
		while (xTaskGetTickCount() == ticks)
			sched_yield();
		return;
	}
}

void paad_sim_freertos_lockstep(struct paad_sim_eth *eth)
{
	lockstep_eth = eth;
}

int paad_sim_freertos_start(struct paad_sim_world *world)
{
	if (irq_task_handle)
		return PAAD_HAL_ERR_STATE;

	sim_world = world;
	paad_sim_set_critical_hooks(critical_enter, critical_exit);

	if (world->mode == PAAD_SIM_REAL_TIME)
		paad_sim_world_set_wake_hook(world, wake_irq_task, NULL);

	if (xTaskCreate(irq_task, "sim irq", IRQ_TASK_STACK, world, configMAX_PRIORITIES - 1, &irq_task_handle) != pdPASS)
		return PAAD_HAL_ERR_NO_MEMORY;

	if (world->mode == PAAD_SIM_VIRTUAL_TIME) {
		taskENTER_CRITICAL();
		tick_base = paad_sim_world_now(world) - (uint64_t)xTaskGetTickCount() * TICK_NS;
		taskEXIT_CRITICAL();
		paad_sim_freertos_set_idle(idle_advance);
		paad_sim_freertos_gate_ticks(true);
	}

	return PAAD_HAL_OK;
}
