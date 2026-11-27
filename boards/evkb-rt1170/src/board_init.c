/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>

#include "board.h"        /* NXP EVKB board support (MPU, debug console) */
#include "clock_config.h"
#include "pin_mux.h"

#include "FreeRTOS.h"
#include "task.h"

#include "openpaad/board.h"

void openpaad_board_init(void)
{
	BOARD_ConfigMPU();
	BOARD_InitPins();
	BOARD_BootClockRUN();
	BOARD_InitDebugConsole();

	openpaad_board_clocks_init();
	openpaad_board_enet_init();
}

/* --- FreeRTOS hooks --------------------------------------------------------- */

void openpaad_freertos_assert(const char *file, int line)
{
	taskDISABLE_INTERRUPTS();
	printf("ASSERT %s:%d\r\n", file, line);
	for (;;) {
	}
}

void vApplicationMallocFailedHook(void)
{
	openpaad_freertos_assert("malloc failed", 0);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
	(void)task;
	openpaad_freertos_assert(name, 0);
}

/* Static memory for the idle and timer tasks (configSUPPORT_STATIC_ALLOCATION). */
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, configSTACK_DEPTH_TYPE *size)
{
	static StaticTask_t idle_tcb;
	static StackType_t idle_stack[configMINIMAL_STACK_SIZE];

	*tcb = &idle_tcb;
	*stack = idle_stack;
	*size = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **tcb, StackType_t **stack, configSTACK_DEPTH_TYPE *size)
{
	static StaticTask_t timer_tcb;
	static StackType_t timer_stack[configTIMER_TASK_STACK_DEPTH];

	*tcb = &timer_tcb;
	*stack = timer_stack;
	*size = configTIMER_TASK_STACK_DEPTH;
}
