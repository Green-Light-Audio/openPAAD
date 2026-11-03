/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * openpaad-sim audio demo: one AAF stream, 2 channels of 32-bit samples at
 * 48 kHz, class A, from device a (talker) to device b (listener).
 *
 * The talker's media clock is derived from gPTP (GenAVB's PTP clock
 * source), the reservation is made through GenAVB's MSRP control API, and
 * the samples count: channel 0 carries the frame number, channel 1 its
 * complement. The listener checks that every frame arrives, in order, and
 * how long before its presentation time it arrives.
 *
 * Run it in virtual time (--virtual): every frame arrives, checked end to
 * end by tests/e2e/audio.sh. In real time FreeRTOS cannot keep GenAVB's
 * 125 us network and media clock period on a host, GenAVB's gPTP-derived
 * media clock then misses timestamps, and the stream loses data
 * (decision 0013).
 */

#include <inttypes.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "genavb/genavb.h"

#include "paad/sim/console.h"

#include "audio.h"

#define SAMPLE_RATE      48000
#define CHANNELS         2
#define FRAME_BYTES      (CHANNELS * 4)
#define BATCH_FRAMES     48 /* 1 ms */
#define STREAM_UID       1
#define TASK_PRIORITY    (tskIDLE_PRIORITY + 2)
#define TASK_STACK       (configMINIMAL_STACK_SIZE * 2)

/* AAF, 2 channels, 32/32 bits, 48 kHz, 6 frames per packet */
static const uint64_t aaf_format = 0x0205022000806000ULL;
static const uint8_t dst_mac[6] = { 0x91, 0xe0, 0xf0, 0x00, 0xfe, 0x01 };

static struct {
	bool talker;
	struct genavb_stream_handle *stream;
	struct genavb_stream_params params;
	unsigned int batch;
	uint64_t t0; /* gPTP time of frame 0 (talker) */

	/* statistics, per report period */
	uint64_t frames;
	uint64_t gaps;
	uint64_t short_writes;

	/* listener state */
	bool started;
	uint32_t next;            /* expected frame number */
	int32_t margin_min_ns;    /* presentation time - arrival, smallest */
	int32_t margin_max_ns;
	bool margin_valid;
} audio;

static void put_be64(uint8_t *p, uint64_t v)
{
	int i;

	for (i = 7; i >= 0; i--, v >>= 8)
		p[i] = (uint8_t)v;
}

static void stream_params(struct genavb_stream_params *p, avtp_direction_t dir, const uint8_t talker_mac[6])
{
	memset(p, 0, sizeof(*p));
	p->direction = dir;
	p->subtype = AVTP_SUBTYPE_AAF;
	p->port = 0;
	p->stream_class = SR_CLASS_A;
	memcpy(p->stream_id, talker_mac, 6);
	p->stream_id[7] = STREAM_UID;
	memcpy(p->dst_mac, dst_mac, 6);
	put_be64((uint8_t *)&p->format, aaf_format);
	p->clock_domain = GENAVB_CLOCK_DOMAIN_0;
	p->talker.latency = 500000; /* process every 0.5 ms (GenAVB minimum) */
	/* A host is not a real-time target: give frames more time to reach
	 * the listener than the class A default (2 ms). */
	p->flags = dir == AVTP_DIRECTION_TALKER ? GENAVB_STREAM_FLAGS_MAX_TRANSIT_TIME_VALID : 0;
	p->talker.max_transit_time = 10000000;
	p->talker.vlan_id = htons(VLAN_VID_DEFAULT);
}

static int control(genavb_control_id_t id, genavb_msg_type_t type, const void *msg, unsigned int len)
{
	struct genavb_control_handle *ctrl;
	uint8_t rsp[64];
	unsigned int rsp_len = sizeof(rsp);
	genavb_msg_type_t t = type;
	int rc;

	rc = genavb_control_open(NULL, &ctrl, id);
	if (rc != GENAVB_SUCCESS)
		return rc;

	rc = genavb_control_send_sync(ctrl, &t, msg, len, rsp, &rsp_len, 1000);
	genavb_control_close(ctrl);

	return rc;
}

static uint64_t gptp_now(void)
{
	uint64_t now = 0;

	genavb_clock_gettime64(GENAVB_CLOCK_GPTP_0_0, &now);

	return now;
}

/* --- Talker --------------------------------------------------------------- */

static void talker_task(void *arg)
{
	uint8_t buf[BATCH_FRAMES * FRAME_BYTES];
	uint32_t frame = 0;

	(void)arg;

	audio.t0 = gptp_now();

	for (;;) {
		/* Frames due by now on the gPTP-derived media clock. */
		uint64_t due = (gptp_now() - audio.t0) * SAMPLE_RATE / 1000000000ULL;

		while (due - frame >= BATCH_FRAMES) {
			unsigned int i;
			int rc;

			for (i = 0; i < BATCH_FRAMES; i++) {
				uint32_t s[CHANNELS] = { frame + i, ~(frame + i) };

				memcpy(&buf[i * FRAME_BYTES], s, sizeof(s));
			}

			rc = genavb_stream_send(audio.stream, buf, sizeof(buf), NULL, 0);
			if (rc < (int)sizeof(buf)) {
				/* stack full (no reservation yet): try again later */
				audio.short_writes++;
				break;
			}

			frame += BATCH_FRAMES;
			audio.frames += BATCH_FRAMES;
		}

		vTaskDelay(pdMS_TO_TICKS(1));
	}
}

int audio_talker_start(struct genavb_handle *genavb, const uint8_t mac[6])
{
	struct genavb_msg_clock_domain_set_source src = {
		.domain = GENAVB_CLOCK_DOMAIN_0,
		.source_type = GENAVB_CLOCK_SOURCE_TYPE_INTERNAL,
		.local_id = GENAVB_CLOCK_SOURCE_PTP_CLK,
	};
	struct genavb_msg_talker_register reg = { 0 };
	unsigned int max_frame_size, max_interval_frames;
	int rc;

	audio.talker = true;
	stream_params(&audio.params, AVTP_DIRECTION_TALKER, mac);

	rc = control(GENAVB_CTRL_CLOCK_DOMAIN, GENAVB_MSG_CLOCK_DOMAIN_SET_SOURCE, &src, sizeof(src));
	if (rc != GENAVB_SUCCESS)
		return rc;

	avdecc_fmt_tspec(&audio.params.format, SR_CLASS_A, &max_frame_size, &max_interval_frames);
	audio.params.talker.max_frame_size = (uint16_t)max_frame_size;
	audio.params.talker.max_interval_frames = (uint16_t)max_interval_frames;

	audio.batch = BATCH_FRAMES * FRAME_BYTES;
	rc = genavb_stream_create(genavb, &audio.stream, &audio.params, &audio.batch, 0);
	if (rc != GENAVB_SUCCESS)
		return rc;

	reg.port = 0;
	memcpy(reg.stream_id, audio.params.stream_id, 8);
	reg.params.stream_class = SR_CLASS_A;
	memcpy(reg.params.destination_address, dst_mac, 6);
	reg.params.vlan_id = VLAN_VID_DEFAULT;
	reg.params.max_frame_size = (uint16_t)max_frame_size;
	reg.params.max_interval_frames = (uint16_t)max_interval_frames;
	reg.params.rank = NORMAL;

	rc = control(GENAVB_CTRL_MSRP, GENAVB_MSG_TALKER_REGISTER, &reg, sizeof(reg));
	if (rc != GENAVB_SUCCESS)
		return rc;

	if (xTaskCreate(talker_task, "audio talker", TASK_STACK, NULL, TASK_PRIORITY, NULL) != pdPASS)
		return -GENAVB_ERR_NO_MEMORY;

	return GENAVB_SUCCESS;
}

/* --- Listener ------------------------------------------------------------- */

static void listener_check(const uint8_t *buf, unsigned int len, const struct genavb_event *ev, unsigned int n_ev,
			   uint32_t now)
{
	unsigned int i, frames = len / FRAME_BYTES;

	for (i = 0; i < frames; i++) {
		uint32_t s[CHANNELS];

		memcpy(s, &buf[i * FRAME_BYTES], sizeof(s));

		if (s[1] != ~s[0]) {
			audio.gaps++; /* corrupted frame */
			continue;
		}

		if (audio.started && s[0] != audio.next)
			audio.gaps++;

		audio.started = true;
		audio.next = s[0] + 1;
		audio.frames++;
	}

	for (i = 0; i < n_ev; i++) {
		int32_t margin;

		if (ev[i].event_mask & AVTP_TIMESTAMP_INVALID)
			continue;

		/* time left before the frame must be played */
		margin = (int32_t)(ev[i].ts - now);
		if (!audio.margin_valid || margin < audio.margin_min_ns)
			audio.margin_min_ns = margin;
		if (!audio.margin_valid || margin > audio.margin_max_ns)
			audio.margin_max_ns = margin;
		audio.margin_valid = true;
	}
}

static void listener_task(void *arg)
{
	static uint8_t buf[16 * BATCH_FRAMES * FRAME_BYTES];
	struct genavb_event ev[64];

	(void)arg;

	for (;;) {
		for (;;) {
			unsigned int n_ev = 64;
			uint32_t now = (uint32_t)gptp_now();
			int rc = genavb_stream_receive(audio.stream, buf, sizeof(buf), ev, &n_ev);

			if (rc <= 0)
				break;

			listener_check(buf, (unsigned int)rc, ev, n_ev, now);
		}

		vTaskDelay(pdMS_TO_TICKS(1));
	}
}

int audio_listener_start(struct genavb_handle *genavb, const uint8_t talker_mac[6])
{
	struct genavb_msg_listener_register reg = { 0 };
	int rc;

	audio.talker = false;
	stream_params(&audio.params, AVTP_DIRECTION_LISTENER, talker_mac);

	audio.batch = BATCH_FRAMES * FRAME_BYTES;
	rc = genavb_stream_create(genavb, &audio.stream, &audio.params, &audio.batch, 0);
	if (rc != GENAVB_SUCCESS)
		return rc;

	reg.port = 0;
	memcpy(reg.stream_id, audio.params.stream_id, 8);
	rc = control(GENAVB_CTRL_MSRP, GENAVB_MSG_LISTENER_REGISTER, &reg, sizeof(reg));
	if (rc != GENAVB_SUCCESS)
		return rc;

	if (xTaskCreate(listener_task, "audio listener", TASK_STACK, NULL, TASK_PRIORITY, NULL) != pdPASS)
		return -GENAVB_ERR_NO_MEMORY;

	return GENAVB_SUCCESS;
}

/* --- Report --------------------------------------------------------------- */

void audio_report(const char *tag, unsigned int t)
{
	if (!audio.stream)
		return;

	taskENTER_CRITICAL();
	uint64_t frames = audio.frames, gaps = audio.gaps, shorts = audio.short_writes;
	int32_t mmin = audio.margin_min_ns, mmax = audio.margin_max_ns;
	bool mvalid = audio.margin_valid;
	audio.frames = 0;
	audio.gaps = 0;
	audio.short_writes = 0;
	audio.margin_valid = false;
	taskEXIT_CRITICAL();

	if (audio.talker) {
		paad_sim_printf("%s: t=%us audio talker: sent %" PRIu64 " frames, %" PRIu64 " waits for the stack\n", tag, t,
				frames, shorts);
		return;
	}

	if (mvalid)
		paad_sim_printf("%s: t=%us audio listener: received %" PRIu64 " frames, %" PRIu64
				" gaps, presentation in %d..%d us\n",
				tag, t, frames, gaps, mmin / 1000, mmax / 1000);
	else
		paad_sim_printf("%s: t=%us audio listener: received %" PRIu64 " frames, %" PRIu64 " gaps\n", tag, t,
				frames, gaps);
}
