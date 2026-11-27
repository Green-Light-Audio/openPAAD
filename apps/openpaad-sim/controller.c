/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * openpaad-sim scripted ATDECC controller.
 *
 * What a controller such as Hive does when a user connects two devices: set
 * the listener's clock domain to follow the stream input (AECP
 * SET_CLOCK_SOURCE, first connection only), then bind the listener's stream
 * input to the talker's stream output (ACMP BIND_RX, named CONNECT_RX
 * before Milan), or unbind it (UNBIND_RX). The listener does the rest: it
 * asks the talker for the stream's parameters and reserves it with SRP.
 *
 * The controller is a second ATDECC entity of one of the devices; its
 * commands go out on the network like any controller's. Like a real one, it
 * only acts on entities it has discovered: an action waits until both are.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "genavb/genavb.h"

#include "paad/sim/console.h"

#include "app.h"
#include "controller.h"
#include "entity.h"

#define MAX_STEPS     16
#define TIMEOUT_MS    5000
#define DISCOVERY_MS  30000 /* longest wait for the entities to be discovered */
#define POLL_MS       100
#define TASK_PRIORITY (tskIDLE_PRIORITY + 2)
#define TASK_STACK    (configMINIMAL_STACK_SIZE * 2)

struct step {
	unsigned int t; /* simulated seconds */
	bool connect;
};

static struct step steps[MAX_STEPS];
static unsigned int num_steps;

static struct {
	struct genavb_handle *genavb;
	struct paad_sim_world *world;
	uint64_t start_ns;
	const char *tag;
	uint64_t talker;
	uint64_t listener;
	struct genavb_control_handle *ctrl;
	bool clock_set;
} ctl;

int controller_parse(const char *script)
{
	const char *p = script;

	num_steps = 0;

	while (*p) {
		unsigned long t;
		bool connect;
		char *end;

		if (!strncmp(p, "connect@", 8)) {
			connect = true;
			p += 8;
		} else if (!strncmp(p, "disconnect@", 11)) {
			connect = false;
			p += 11;
		} else {
			return -1;
		}

		t = strtoul(p, &end, 10);
		if (end == p || num_steps == MAX_STEPS || (num_steps && t < steps[num_steps - 1].t))
			return -1;

		steps[num_steps].t = (unsigned int)t;
		steps[num_steps].connect = connect;
		num_steps++;

		p = end;
		if (*p == ',')
			p++;
		else if (*p)
			return -1;
	}

	return num_steps ? 0 : -1;
}

static const char *acmp_status_name(int status)
{
	static const char *const names[] = {
		[ACMP_STAT_SUCCESS] = "SUCCESS",
		[ACMP_STAT_LISTENER_UNKNOWN_ID] = "LISTENER_UNKNOWN_ID",
		[ACMP_STAT_TALKER_UNKNOWN_ID] = "TALKER_UNKNOWN_ID",
		[ACMP_STAT_TALKER_DEST_MAC_FAIL] = "TALKER_DEST_MAC_FAIL",
		[ACMP_STAT_TALKER_NO_STREAM_INDEX] = "TALKER_NO_STREAM_INDEX",
		[ACMP_STAT_TALKER_NO_BANDWIDTH] = "TALKER_NO_BANDWIDTH",
		[ACMP_STAT_TALKER_EXCLUSIVE] = "TALKER_EXCLUSIVE",
		[ACMP_STAT_LISTENER_TALKER_TIMEOUT] = "LISTENER_TALKER_TIMEOUT",
		[ACMP_STAT_LISTENER_EXCLUSIVE] = "LISTENER_EXCLUSIVE",
		[ACMP_STAT_STATE_UNAVAILABLE] = "STATE_UNAVAILABLE",
		[ACMP_STAT_NOT_CONNECTED] = "NOT_CONNECTED",
		[ACMP_STAT_NO_SUCH_CONNECTION] = "NO_SUCH_CONNECTION",
		[ACMP_STAT_COULD_NOT_SEND_MESSAGE] = "COULD_NOT_SEND_MESSAGE",
		[ACMP_STAT_TALKER_MISBEHAVING] = "TALKER_MISBEHAVING",
		[ACMP_STAT_LISTENER_MISBEHAVING] = "LISTENER_MISBEHAVING",
		[ACMP_STAT_CONTROLLER_NOT_AUTHORIZED] = "CONTROLLER_NOT_AUTHORIZED",
		[ACMP_STAT_INCOMPATIBLE_REQUEST] = "INCOMPATIBLE_REQUEST",
		[ACMP_STAT_NOT_SUPPORTED] = "NOT_SUPPORTED",
	};

	if (status >= 0 && (size_t)status < sizeof(names) / sizeof(names[0]) && names[status])
		return names[status];

	return "unknown status";
}

/* An ACMP command to the listener; returns the ACMP status, or a negative
 * GenAVB error if no response came. */
static int acmp_command(acmp_message_type_t type)
{
	struct genavb_acmp_command cmd;
	struct genavb_acmp_response rsp;
	genavb_msg_type_t msg_type = GENAVB_MSG_ACMP_COMMAND;
	unsigned int len = sizeof(rsp);
	int rc;

	memset(&cmd, 0, sizeof(cmd));
	cmd.message_type = type;
	cmd.talker_entity_id = htonll(ctl.talker);
	cmd.listener_entity_id = htonll(ctl.listener);
	cmd.talker_unique_id = htons(ENTITY_AUDIO_OUTPUT);
	cmd.listener_unique_id = htons(ENTITY_AUDIO_INPUT);

	rc = genavb_control_send_sync(ctl.ctrl, &msg_type, &cmd, sizeof(cmd), &rsp, &len, TIMEOUT_MS);
	if (rc != GENAVB_SUCCESS)
		return rc < 0 ? rc : -rc;
	if (msg_type != GENAVB_MSG_ACMP_RESPONSE)
		return -GENAVB_ERR_CTRL_INVALID;

	return rsp.status;
}

/* AECP SET_CLOCK_SOURCE on the listener's clock domain 0; returns the AECP
 * status, or a negative GenAVB error if no response came. */
static int set_clock_source(uint16_t source)
{
	struct genavb_aecp_msg msg;
	struct aecp_aem_pdu *pdu = (struct aecp_aem_pdu *)msg.buf;
	struct aecp_aem_set_clock_source_pdu *cmd = (struct aecp_aem_set_clock_source_pdu *)(pdu + 1);
	genavb_msg_type_t msg_type = GENAVB_MSG_AECP;
	unsigned int len = sizeof(msg);
	uint64_t id = htonll(ctl.listener);
	int rc;

	memset(&msg, 0, sizeof(msg));
	msg.msg_type = AECP_AEM_COMMAND;
	msg.len = sizeof(*pdu) + sizeof(*cmd);
	memcpy(&pdu->entity_id, &id, sizeof(id));
	AECP_AEM_SET_U_CMD_TYPE(pdu, 0, AECP_AEM_CMD_SET_CLOCK_SOURCE);
	cmd->descriptor_type = htons(AEM_DESC_TYPE_CLOCK_DOMAIN);
	cmd->descriptor_index = htons(0);
	cmd->clock_source_index = htons(source);

	rc = genavb_control_send_sync(ctl.ctrl, &msg_type, &msg, sizeof(msg), &msg, &len, TIMEOUT_MS);
	if (rc != GENAVB_SUCCESS)
		return rc < 0 ? rc : -rc;

	return msg.status;
}

/* Seconds since t=0, to a tenth, for the log. */
static void elapsed(char *buf, size_t len)
{
	uint64_t ds = (paad_sim_world_now(ctl.world) - ctl.start_ns) / 100000000ULL;

	snprintf(buf, len, "%" PRIu64 ".%" PRIu64, ds / 10, ds % 10);
}

/* Notifications the controller does not use (entities appearing...). */
static void drain(void)
{
	union genavb_controller_msg msg;
	genavb_msg_type_t type;
	unsigned int len = sizeof(msg);

	while (genavb_control_receive(ctl.ctrl, &type, &msg, &len) == GENAVB_SUCCESS)
		len = sizeof(msg);
}

static bool discovered(uint64_t id)
{
	struct genavb_adp_msg req, rsp;
	genavb_msg_type_t type = GENAVB_MSG_ADP;
	unsigned int len = sizeof(rsp);

	memset(&req, 0, sizeof(req));
	req.msg_type = ADP_ENTITY_AVAILABLE;
	req.info.entity_id = htonll(id);

	if (genavb_control_send_sync(ctl.ctrl, &type, &req, sizeof(req), &rsp, &len, TIMEOUT_MS) != GENAVB_SUCCESS)
		return false;

	return type == GENAVB_MSG_ADP && rsp.msg_type == ADP_ENTITY_AVAILABLE;
}

static bool wait_discovered(void)
{
	unsigned int waited;

	for (waited = 0; waited < DISCOVERY_MS; waited += POLL_MS) {
		drain();
		if (discovered(ctl.talker) && discovered(ctl.listener))
			return true;
		vTaskDelay(pdMS_TO_TICKS(POLL_MS));
	}

	return false;
}

static void run_step(const struct step *s)
{
	char t[16];
	int rc;

	if (!wait_discovered()) {
		elapsed(t, sizeof(t));
		paad_sim_printf("%s: t=%ss controller: %016" PRIx64 " or %016" PRIx64 " not discovered, nothing done\n",
				ctl.tag, t, ctl.talker, ctl.listener);
		return;
	}

	if (s->connect && !ctl.clock_set) {
		rc = set_clock_source(ENTITY_CLOCK_SOURCE_AUDIO_IN);
		elapsed(t, sizeof(t));
		if (rc < 0)
			paad_sim_printf("%s: t=%ss controller: clock source of %016" PRIx64 ": no response (%s)\n",
					ctl.tag, t, ctl.listener, genavb_strerror(rc));
		else
			paad_sim_printf("%s: t=%ss controller: clock source of %016" PRIx64 " set to its audio input: %s\n",
					ctl.tag, t, ctl.listener, rc == AECP_AEM_SUCCESS ? "SUCCESS" : "refused");
		ctl.clock_set = rc == AECP_AEM_SUCCESS;
	}

	rc = acmp_command(s->connect ? ACMP_BIND_RX_COMMAND : ACMP_UNBIND_RX_COMMAND);
	elapsed(t, sizeof(t));
	if (rc < 0)
		paad_sim_printf("%s: t=%ss controller: %s %016" PRIx64 " input %u %s %016" PRIx64
				" output %u: no response (%s)\n",
				ctl.tag, t, s->connect ? "connect" : "disconnect", ctl.listener, ENTITY_AUDIO_INPUT,
				s->connect ? "to" : "from", ctl.talker, ENTITY_AUDIO_OUTPUT, genavb_strerror(rc));
	else
		paad_sim_printf("%s: t=%ss controller: %s %016" PRIx64 " input %u %s %016" PRIx64 " output %u: %s\n",
				ctl.tag, t, s->connect ? "connect" : "disconnect", ctl.listener, ENTITY_AUDIO_INPUT,
				s->connect ? "to" : "from", ctl.talker, ENTITY_AUDIO_OUTPUT, acmp_status_name(rc));
}

static void controller_task(void *arg)
{
	unsigned int i;

	(void)arg;

	for (i = 0; i < num_steps; i++) {
		app_sleep_until(ctl.world, ctl.start_ns + (uint64_t)steps[i].t * 1000000000ULL);
		run_step(&steps[i]);
	}

	vTaskDelete(NULL);
}

int controller_start(struct genavb_handle *genavb, struct paad_sim_world *world, uint64_t start_ns, const char *tag,
		     uint64_t talker, uint64_t listener)
{
	int rc;

	ctl.genavb = genavb;
	ctl.world = world;
	ctl.start_ns = start_ns;
	ctl.tag = tag;
	ctl.talker = talker;
	ctl.listener = listener;

	rc = genavb_control_open(genavb, &ctl.ctrl, GENAVB_CTRL_AVDECC_CONTROLLER);
	if (rc != GENAVB_SUCCESS)
		return rc;

	if (xTaskCreate(controller_task, "controller", TASK_STACK, NULL, TASK_PRIORITY, NULL) != pdPASS)
		return -GENAVB_ERR_NO_MEMORY;

	return GENAVB_SUCCESS;
}
