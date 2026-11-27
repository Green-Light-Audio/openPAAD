/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * openpaad-sim: the openPAAD stack on simulated hardware, on a PC or Mac.
 *
 * GenAVB runs unchanged under FreeRTOS (POSIX port) on the simulated board
 * of hal/sim, through the HAL driver of paad/genavb. Two modes:
 *
 * Bench (default): one process, two network ports joined by a virtual
 * cable. Port 0 runs the full stack and is grandmaster capable; a second
 * gPTP instance runs on port 1, whose clock starts 1000 s away and ticks
 * 20 ppm fast, and must lock onto port 0. Prints port 1's gPTP time minus
 * port 0's every second.
 *
 * Both run in real time by default, or in virtual time (--virtual).
 *
 * Device (--cable NAME --side a|b): one process is one device running the
 * full stack on its port 0, cabled to the device of another process on the
 * same host. With --net IFNAME instead of --cable, port 0 is plugged into a
 * network interface of the host, where real equipment and software (an
 * ATDECC controller such as Hive, another simulated device) can reach it. Side a is grandmaster capable and its clock is exact; side b
 * starts 1000 s away, ticks 20 ppm fast and is not grandmaster capable.
 * Each device is a Milan-mode ATDECC entity with one audio stream each way
 * (entity.c); it streams audio when a controller connects it (audio.c), and
 * recovers its media clock (media_clock.c). Every second it prints its gPTP
 * time minus the clock both devices share (once locked, both sides print
 * the same value), its streams and its media clock.
 *
 * Usage: openpaad-sim [-t seconds] [--virtual] [--check-gptp ns]
 *                     [--cable NAME | --net IFNAME] [--side a|b] [--control SCRIPT]
 *                     [-v | --log file]
 *   -t seconds        run for this long, then exit (default: forever)
 *   --check-gptp ns   bench: exit with status 0 only if, over the last 5
 *                     seconds, port 1's gPTP time stayed within ns of port 0's
 *   --cable NAME      device mode, cabled through /tmp/openpaad-NAME-<side>.sock
 *   --net IFNAME      device mode, on the host's interface IFNAME (real time;
 *                     needs the right to capture on it, e.g. root)
 *   --side a|b        which device (MAC address, clock); a is grandmaster
 *                     capable
 *   --log file        write GenAVB's log to this file (default:
 *                     openpaad-sim.log, or openpaad-sim-<side>.log)
 *   -v                write GenAVB's log to the terminal instead
 *   --control SCRIPT  device mode: also run a controller that connects b's
 *                     audio input to a's audio output, or disconnects it, at
 *                     given seconds: "connect@2,disconnect@10,connect@14"
 *   --virtual         simulate in virtual time (decision 0013): exact timing
 *                     whatever the host's load; both devices of a cable must
 *                     use it, and -t counts simulated seconds
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

#include "paad/sim/console.h"
#include "paad/sim/eth.h"
#include "paad/sim/freertos.h"
#include "paad/sim/platform.h"

#include "app.h"
#include "audio.h"
#include "controlled.h"
#include "controller.h"
#include "entity.h"
#include "media_clock.h"

/* GenAVB internals: start the management (MAC service) and gPTP instances
 * of a second endpoint port (api/rtos/init.c starts those of the first),
 * and convert a port's hardware time to one of its clocks. */
extern void *management_task_init(struct management_config *cfg);
extern void *gptp_task_init(struct fgptp_config *cfg);
extern int clock_time_from_hw(os_clock_id_t id, uint64_t hw_ns, uint64_t *ns);

#define CHECK_WINDOW_S 5
#define CABLE_NS       500

static unsigned int run_seconds;
static int64_t check_ns = -1;
static const char *log_path;
static bool verbose;
static const char *cable_name;
static const char *net_if;
static bool device_mode; /* --cable or --net */
static char side; /* 'a' or 'b' in device mode */
static char tag[16] = "openpaad-sim";
static const char *control_script;
static bool virtual_time;

static struct genavb_handle *genavb;
static struct genavb_config config;
static struct management_config management_port1;
static struct fgptp_config gptp_port1;

/* A port's gPTP time at a simulated instant, exactly: its hardware time at
 * that instant converted by GenAVB. */
static int gptp_time_at(unsigned int port, uint64_t source_ns, uint64_t *gptp_ns)
{
	struct paad_sim_platform *board = paad_sim_platform();
	os_clock_id_t id = port ? OS_CLOCK_GPTP_EP_1_0 : OS_CLOCK_GPTP_EP_0_0;

	return clock_time_from_hw(id, paad_sim_clock_at(&board->clock[port], source_ns), gptp_ns);
}

/* Bench: port 1 gPTP time minus port 0 gPTP time, for one simulated
 * instant. Reading the two clocks one after the other instead would add
 * the time between the reads. */
static int bench_offset(int64_t *offset)
{
	uint64_t now, g0, g1;
	int rc;

	taskENTER_CRITICAL();
	now = paad_sim_world_now(&paad_sim_platform()->world);
	rc = gptp_time_at(0, now, &g0);
	rc |= gptp_time_at(1, now, &g1);
	taskEXIT_CRITICAL();

	if (rc < 0)
		return -1;

	*offset = (int64_t)(g1 - g0);

	return 0;
}

/* Device: a simulated instant and its gPTP time. gPTP time minus that
 * instant compares the device with the clock shared by the simulated
 * devices (the host's monotonic clock, or virtual time in lockstep). */
static int device_instant(uint64_t *now, uint64_t *gptp_ns)
{
	int rc;

	taskENTER_CRITICAL();
	*now = paad_sim_world_now(&paad_sim_platform()->world);
	rc = gptp_time_at(0, *now, gptp_ns);
	taskEXIT_CRITICAL();

	return rc;
}

static void finish(int status)
{
	struct paad_sim_platform *board = paad_sim_platform();

	/* the other device stops waiting for us */
	if (cable_name && board)
		paad_sim_eth_cable_bye(&board->eth[0]);

	paad_sim_console_flush();
	_exit(status); /* the FreeRTOS scheduler never returns to main() */
}

void app_sleep_until(struct paad_sim_world *world, uint64_t when)
{
	for (;;) {
		uint64_t now = paad_sim_world_now(world);

		if (now >= when)
			return;

		vTaskDelay(pdMS_TO_TICKS((uint32_t)((when - now + 999999) / 1000000)));
	}
}

static void start_stack(void)
{
	int rc;

	genavb_get_default_config(&config);
	config.fgptp_config.domain_cfg[0].gmCapable = (!device_mode || side == 'a') ? 1 : 0;

	/* A host interface gives software timestamps only: tens of
	 * microseconds of jitter, where gPTP's usual 800 ns limit on the link
	 * delay assumes hardware ones. Accept up to 1 ms there, so that gPTP
	 * still runs, coarsely. */
	if (net_if)
		config.fgptp_config.neighborPropDelayThreshold = 1000000;

	if (device_mode) {
		char name[32];

		snprintf(name, sizeof(name), "openpaad-sim %c", side);
		if (entity_configure(&config.avdecc_config, paad_sim_platform()->cfg.mac[0], name, control_script != NULL) < 0) {
			paad_sim_printf("%s: cannot build the ATDECC entities\n", tag);
			finish(2);
		}
	}

	if (!device_mode) {
		/* Bench: same settings on port 1, not grandmaster capable. */
		management_port1 = config.management_config;
		management_port1.logical_port_list[0] = 1;
		management_port1.port_max = 1;

		gptp_port1 = config.fgptp_config;
		gptp_port1.logical_port_list[0] = 1;
		gptp_port1.port_max = 1;
		gptp_port1.domain_cfg[0].gmCapable = 0;
	}

	genavb_set_config(&config);

	rc = genavb_init(&genavb, 0);
	if (rc != GENAVB_SUCCESS) {
		paad_sim_printf("%s: genavb_init() failed: %s\n", tag, genavb_strerror(rc));
		finish(2);
	}

	if (!device_mode && (!management_task_init(&management_port1) || !gptp_task_init(&gptp_port1))) {
		paad_sim_printf("%s: cannot start gPTP on port 1\n", tag);
		finish(2);
	}
}

static void main_task(void *arg)
{
	struct paad_sim_platform *board = paad_sim_platform();
	int64_t worst = 0;
	uint64_t start;
	unsigned int t;

	(void)arg;

	if (paad_sim_freertos_start(&board->world) != PAAD_HAL_OK) {
		paad_sim_printf("%s: cannot start simulated interrupts\n", tag);
		finish(2);
	}

	start_stack();
	start = paad_sim_world_now(&board->world);

	if (device_mode) {
		int rc = controlled_start(genavb, config.avdecc_config.entity_cfg[ENTITY_DEVICE].aem, tag);

		if (rc != GENAVB_SUCCESS) {
			paad_sim_printf("%s: cannot answer the commands left to the application: %s\n", tag,
					genavb_strerror(rc));
			finish(2);
		}

		rc = audio_start(genavb, tag);
		if (rc != GENAVB_SUCCESS) {
			paad_sim_printf("%s: cannot listen for stream connections: %s\n", tag, genavb_strerror(rc));
			finish(2);
		}
	}

	if (control_script) {
		/* b's audio input to a's audio output (port 0 MAC addresses:
		 * a ...:01, b ...:02). */
		uint8_t mac_a[6], mac_b[6];
		int rc;

		memcpy(mac_a, board->cfg.mac[0], 6);
		memcpy(mac_b, board->cfg.mac[0], 6);
		mac_a[5] = 1;
		mac_b[5] = 2;
		rc = controller_start(genavb, &board->world, start, tag, entity_id(mac_a, ENTITY_DEVICE),
				      entity_id(mac_b, ENTITY_DEVICE));
		if (rc != GENAVB_SUCCESS) {
			paad_sim_printf("%s: cannot start the controller: %s\n", tag, genavb_strerror(rc));
			finish(2);
		}
	}

	if (device_mode)
		paad_sim_printf("%s: device running on %s %s%s, entity %016" PRIx64 "\n", tag,
				cable_name ? "cable" : "interface", cable_name ? cable_name : net_if,
				virtual_time ? ", virtual time" : "", entity_id(board->cfg.mac[0], ENTITY_DEVICE));
	else
		paad_sim_printf("%s: stack running, port 0 and port 1 joined by a %u ns cable\n", tag, CABLE_NS);

	for (t = 1; !run_seconds || t <= run_seconds; t++) {
		int64_t offset;

		app_sleep_until(&board->world, start + (uint64_t)t * 1000000000ULL);

		if (device_mode) {
			uint64_t now, g;

			if (device_instant(&now, &g) < 0) {
				paad_sim_printf("%s: t=%us cannot read the gPTP clock\n", tag, t);
				continue;
			}
			paad_sim_printf("%s: t=%us gPTP - shared clock: %" PRId64 " ns\n", tag, t, (int64_t)(g - now));
			audio_report(tag, t);
			media_clock_report(tag, t, now, g);
			continue;
		}

		if (bench_offset(&offset) < 0) {
			paad_sim_printf("%s: t=%us cannot read gPTP clocks\n", tag, t);
			continue;
		}

		paad_sim_printf("%s: t=%us gPTP offset port 1 - port 0: %" PRId64 " ns\n", tag, t, offset);

		if (run_seconds && t + CHECK_WINDOW_S > run_seconds) {
			int64_t mag = offset < 0 ? -offset : offset;

			if (mag > worst)
				worst = mag;
		}
	}

	if (check_ns >= 0) {
		paad_sim_printf("%s: worst offset over the last %u s: %" PRId64 " ns (limit %" PRId64 " ns): %s\n", tag,
				CHECK_WINDOW_S, worst, check_ns, worst <= check_ns ? "PASS" : "FAIL");
		finish(worst <= check_ns ? 0 : 1);
	}

	finish(0);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [-t seconds] [--virtual] [--check-gptp ns] [--cable NAME | --net IFNAME] [--side a|b]\n"
		"       [--control SCRIPT] [-v | --log file]\n", prog);
	exit(2);
}

/* The simulated board: two ports. In device mode port 0 goes to the cable
 * and port 1 stays unplugged. */
static int build_board(void)
{
	struct paad_sim_platform_config cfg;
	struct paad_sim_platform *board;
	char local[96], peer[96];
	unsigned int i;

	paad_sim_platform_default_config(&cfg);
	cfg.cable_ns = CABLE_NS;
	if (virtual_time)
		cfg.mode = PAAD_SIM_VIRTUAL_TIME;

	if (device_mode) {
		cfg.cable = false;
		for (i = 0; i < PAAD_SIM_PLATFORM_PORTS; i++)
			cfg.mac[i][5] = (uint8_t)((side == 'a' ? 1 : 2) + 2 * i); /* a: 01, 03; b: 02, 04 */
		/* Relative to the shared clock, so that "gPTP - shared clock"
		 * stays a small number (about 1000 s) whatever the host's uptime. */
		cfg.initial_ns[0] = (virtual_time ? 0 : paad_sim_monotonic_ns(NULL)) +
				    (side == 'a' ? 1000 : 2000) * 1000000000ULL;
		cfg.natural_ppb[0] = side == 'a' ? 0 : 20000;
		/* audio oscillators: a 40 ppm fast, b 30 ppm slow */
		cfg.mclk_natural_ppb = side == 'a' ? 40000 : -30000;
	}

	if (paad_sim_platform_init(&cfg) != PAAD_HAL_OK)
		return -1;

	if (!device_mode)
		return 0;

	board = paad_sim_platform();

	if (net_if) {
		char err[160];

		if (paad_sim_eth_connect_host(&board->eth[0], net_if, err, sizeof(err)) != PAAD_HAL_OK) {
			fprintf(stderr, "--net %s: %s\n", net_if, err);
			return -1;
		}
		return 0;
	}

	snprintf(local, sizeof(local), "/tmp/openpaad-%s-%c.sock", cable_name, side);
	snprintf(peer, sizeof(peer), "/tmp/openpaad-%s-%c.sock", cable_name, side == 'a' ? 'b' : 'a');

	if (paad_sim_eth_connect_socket(&board->eth[0], local, peer, CABLE_NS) != PAAD_HAL_OK)
		return -1;

	if (virtual_time)
		paad_sim_freertos_lockstep(&board->eth[0]);

	return 0;
}

int main(int argc, char **argv)
{
	char default_log[32];
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-t") && i + 1 < argc)
			run_seconds = (unsigned int)strtoul(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--check-gptp") && i + 1 < argc)
			check_ns = strtoll(argv[++i], NULL, 0);
		else if (!strcmp(argv[i], "--cable") && i + 1 < argc)
			cable_name = argv[++i];
		else if (!strcmp(argv[i], "--net") && i + 1 < argc)
			net_if = argv[++i];
		else if (!strcmp(argv[i], "--side") && i + 1 < argc)
			side = argv[++i][0];
		else if (!strcmp(argv[i], "--log") && i + 1 < argc)
			log_path = argv[++i];
		else if (!strcmp(argv[i], "-v"))
			verbose = true;
		else if (!strcmp(argv[i], "--control") && i + 1 < argc)
			control_script = argv[++i];
		else if (!strcmp(argv[i], "--virtual"))
			virtual_time = true;
		else
			usage(argv[0]);
	}

	device_mode = cable_name || net_if;

	if ((control_script && !device_mode) || (cable_name && net_if) || (net_if && virtual_time) ||
	    (device_mode && ((side != 'a' && side != 'b') || check_ns >= 0)) ||
	    (cable_name && (strlen(cable_name) > 40 || strchr(cable_name, '/')))) {
		fprintf(stderr, "--cable or --net (not both) need --side a or b, and no --check-gptp;\n"
				"the cable name must be short, without '/'; --net runs in real time only;\n"
				"--control needs --cable or --net\n");
		return 2;
	}

	if (control_script && controller_parse(control_script) < 0) {
		fprintf(stderr, "--control: expected actions like connect@2,disconnect@10,connect@14\n");
		return 2;
	}

	if (check_ns >= 0 && run_seconds <= CHECK_WINDOW_S) {
		fprintf(stderr, "--check-gptp needs -t longer than %u s\n", CHECK_WINDOW_S);
		return 2;
	}

	if (device_mode)
		snprintf(tag, sizeof(tag), "openpaad-sim[%c]", side);

	setvbuf(stdout, NULL, _IOLBF, 0);

	if (!verbose) {
		FILE *log;

		if (!log_path) {
			if (device_mode)
				snprintf(default_log, sizeof(default_log), "openpaad-sim-%c.log", side);
			else
				snprintf(default_log, sizeof(default_log), "openpaad-sim.log");
			log_path = default_log;
		}

		log = fopen(log_path, "w");
		if (!log) {
			fprintf(stderr, "cannot open %s\n", log_path);
			return 2;
		}
		paad_sim_console_set_log(log);
		printf("%s: GenAVB log in %s (-v to show it here)\n", tag, log_path);
	}

	if (build_board() < 0) {
		fprintf(stderr, "cannot build the simulated board\n");
		return 2;
	}

	if (xTaskCreate(main_task, "openpaad-sim", configMINIMAL_STACK_SIZE * 4, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS)
		return 2;

	vTaskStartScheduler();

	return 2;
}
