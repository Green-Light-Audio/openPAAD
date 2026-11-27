/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * openPAAD reference endpoint.
 *
 * Phase 0 scope: boot the board, start FreeRTOS and bring up the full
 * GenAVB/TSN stack (gPTP, SRP, MAAP, AVTP, AVDECC). Audio streaming and the
 * pro-audio entity model are added in Phases 3 to 5.
 */

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "genavb/genavb.h"

#include "openpaad/board.h"
#include "openpaad/version.h"

#define STACK_TASK_STACK_WORDS 2048
#define STACK_TASK_PRIORITY    (tskIDLE_PRIORITY + 1)

static struct genavb_handle *genavb;

static void stack_task(void *arg)
{
	struct genavb_config config;
	int rc;

	(void)arg;

	printf("\r\nopenPAAD reference endpoint %s\r\n", OPENPAAD_VERSION_STRING);

	genavb_get_default_config(&config);
	genavb_set_config(&config);

	rc = genavb_init(&genavb, 0);
	if (rc != GENAVB_SUCCESS) {
		printf("genavb_init() failed: %s\r\n", genavb_strerror(rc));
		vTaskSuspend(NULL);
	}

	printf("AVB stack running\r\n");

	for (;;)
		vTaskDelay(pdMS_TO_TICKS(1000));
}

int main(void)
{
	openpaad_board_init();

	if (xTaskCreate(stack_task, "openpaad", STACK_TASK_STACK_WORDS, NULL, STACK_TASK_PRIORITY, NULL) != pdPASS) {
		printf("cannot create main task\r\n");
		for (;;) {
		}
	}

	vTaskStartScheduler();

	for (;;) {
	}
}
