/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * openpaad-sim: the openPAAD stack on simulated hardware, on a PC or Mac.
 *
 * GenAVB runs unchanged under FreeRTOS (POSIX port) on the simulated board
 * of hal/sim, through the HAL driver of paad/genavb. The board has two
 * network ports joined by a virtual cable. Port 0 runs the full stack and is
 * grandmaster capable; a second gPTP instance runs on port 1, whose clock
 * starts elsewhere and ticks 20 ppm fast, and must lock onto port 0.
 *
 * Usage: openpaad-sim [-t seconds] [--check-gptp ns]
 *   -t seconds        run for this long, then exit (default: forever)
 *   --check-gptp ns   exit with status 0 only if, over the last 5 seconds,
 *                     port 1's gPTP time stayed within ns of port 0's
 *
 * The simulated host clock has nanosecond resolution on Linux and macOS.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"

#include "genavb/genavb.h"
#include "os/clock.h"

#include "paad/sim/freertos.h"
#include "paad/sim/platform.h"

/* GenAVB internals: start the management (MAC service) and gPTP instances
 * of a second endpoint port (api/rtos/init.c starts those of the first),
 * and convert a port's hardware time to one of its clocks. */
extern void *management_task_init(struct management_config *cfg);
extern void *gptp_task_init(struct fgptp_config *cfg);
extern int clock_time_from_hw(os_clock_id_t id, uint64_t hw_ns, uint64_t *ns);

#define CHECK_WINDOW_S 5

static unsigned int run_seconds;
static int64_t check_ns = -1;

static struct genavb_handle *genavb;
static struct genavb_config config;
static struct management_config management_port1;
static struct fgptp_config gptp_port1;

/* Port 1 gPTP time minus port 0 gPTP time, exactly: both computed for the
 * same simulated instant, from each port's hardware time at that instant
 * and GenAVB's own conversion to gPTP time. Reading the two clocks one
 * after the other instead would add the time between the reads. */
static int read_offset(int64_t *offset)
{
	struct paad_sim_platform *board = paad_sim_platform();
	uint64_t now, g0, g1;
	int rc;

	taskENTER_CRITICAL();
	now = paad_sim_world_now(&board->world);
	rc = clock_time_from_hw(OS_CLOCK_GPTP_EP_0_0, paad_sim_clock_at(&board->clock[0], now), &g0);
	rc |= clock_time_from_hw(OS_CLOCK_GPTP_EP_1_0, paad_sim_clock_at(&board->clock[1], now), &g1);
	taskEXIT_CRITICAL();

	if (rc < 0)
		return -1;

	*offset = (int64_t)(g1 - g0);

	return 0;
}

static void finish(int status)
{
	fflush(stdout);
	fflush(stderr);
	_exit(status);
}

static void main_task(void *arg)
{
	struct paad_sim_platform *board = paad_sim_platform();
	int64_t worst = 0;
	unsigned int t;
	int rc;

	(void)arg;

	if (paad_sim_freertos_start(&board->world) != PAAD_HAL_OK) {
		printf("openpaad-sim: cannot start simulated interrupts\n");
		finish(2);
	}

	genavb_get_default_config(&config);
	config.fgptp_config.domain_cfg[0].gmCapable = 1;

	/* Same settings on port 1, but not grandmaster capable. */
	management_port1 = config.management_config;
	management_port1.logical_port_list[0] = 1;
	management_port1.port_max = 1;

	gptp_port1 = config.fgptp_config;
	gptp_port1.logical_port_list[0] = 1;
	gptp_port1.port_max = 1;
	gptp_port1.domain_cfg[0].gmCapable = 0;

	genavb_set_config(&config);

	rc = genavb_init(&genavb, 0);
	if (rc != GENAVB_SUCCESS) {
		printf("openpaad-sim: genavb_init() failed: %s\n", genavb_strerror(rc));
		finish(2);
	}

	if (!management_task_init(&management_port1) || !gptp_task_init(&gptp_port1)) {
		printf("openpaad-sim: cannot start gPTP on port 1\n");
		finish(2);
	}

	printf("openpaad-sim: stack running, port 0 and port 1 joined by a %" PRIu32 " ns cable\n",
	       board->cfg.cable_ns);

	for (t = 1; !run_seconds || t <= run_seconds; t++) {
		int64_t offset;

		vTaskDelay(pdMS_TO_TICKS(1000));

		if (read_offset(&offset) < 0) {
			printf("openpaad-sim: t=%us cannot read gPTP clocks\n", t);
			continue;
		}

		printf("openpaad-sim: t=%us gPTP offset port 1 - port 0: %" PRId64 " ns\n", t, offset);

		if (run_seconds && t + CHECK_WINDOW_S > run_seconds) {
			int64_t mag = offset < 0 ? -offset : offset;

			if (mag > worst)
				worst = mag;
		}
	}

	if (check_ns >= 0) {
		printf("openpaad-sim: worst offset over the last %u s: %" PRId64 " ns (limit %" PRId64 " ns): %s\n",
		       CHECK_WINDOW_S, worst, check_ns, worst <= check_ns ? "PASS" : "FAIL");
		finish(worst <= check_ns ? 0 : 1);
	}

	finish(0);
}

static void usage(const char *prog)
{
	fprintf(stderr, "usage: %s [-t seconds] [--check-gptp ns]\n", prog);
	exit(2);
}

int main(int argc, char **argv)
{
	struct paad_sim_platform_config cfg;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-t") && i + 1 < argc)
			run_seconds = (unsigned int)strtoul(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--check-gptp") && i + 1 < argc)
			check_ns = strtoll(argv[++i], NULL, 0);
		else
			usage(argv[0]);
	}

	if (check_ns >= 0 && run_seconds <= CHECK_WINDOW_S) {
		fprintf(stderr, "--check-gptp needs -t longer than %u s\n", CHECK_WINDOW_S);
		return 2;
	}

	setvbuf(stdout, NULL, _IOLBF, 0);

	paad_sim_platform_default_config(&cfg);
	if (paad_sim_platform_init(&cfg) != PAAD_HAL_OK) {
		fprintf(stderr, "cannot build the simulated board\n");
		return 2;
	}

	if (xTaskCreate(main_task, "openpaad-sim", configMINIMAL_STACK_SIZE * 4, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS)
		return 2;

	vTaskStartScheduler();

	return 2;
}
