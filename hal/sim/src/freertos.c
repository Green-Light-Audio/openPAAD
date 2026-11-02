/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "paad/hal/common.h"
#include "paad/sim/freertos.h"
#include "paad/sim/hooks.h"

#define IRQ_TASK_STACK (configMINIMAL_STACK_SIZE * 4)

static TaskHandle_t irq_task_handle;

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

static void irq_task(void *arg)
{
	struct paad_sim_world *world = arg;
	const uint64_t tick_ns = 1000000000ULL / configTICK_RATE_HZ;

	for (;;) {
		uint64_t next = paad_sim_world_run_pending(world);
		uint64_t now = paad_sim_world_now(world);
		TickType_t ticks;

		if (next == UINT64_MAX)
			ticks = portMAX_DELAY;
		else if (next <= now)
			continue;
		else
			ticks = (TickType_t)((next - now + tick_ns - 1) / tick_ns);

		ulTaskNotifyTake(pdTRUE, ticks);
	}
}

int paad_sim_freertos_start(struct paad_sim_world *world)
{
	if (irq_task_handle)
		return PAAD_HAL_ERR_STATE;

	paad_sim_set_critical_hooks(critical_enter, critical_exit);
	paad_sim_world_set_wake_hook(world, wake_irq_task, NULL);

	if (xTaskCreate(irq_task, "sim irq", IRQ_TASK_STACK, world, configMAX_PRIORITIES - 1, &irq_task_handle) != pdPASS)
		return PAAD_HAL_ERR_NO_MEMORY;

	return PAAD_HAL_OK;
}
