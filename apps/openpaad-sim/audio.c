/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * openpaad-sim audio: the streams ATDECC connects.
 *
 * GenAVB's ATDECC stack decides which streams run (entity.c): when a
 * controller binds a listener's input to a talker's output and SRP has
 * reserved the path, it tells both applications to open the stream, and to
 * close it when the binding goes. This module does what a device's audio
 * application does with that: it opens and closes the streams, and sets the
 * clock domain's source as the entity says (internal, or a stream input for
 * media clock recovery).
 *
 * Audio: AAF, 2 channels of 32-bit samples at 48 kHz. The talker's samples
 * count: channel 0 carries the frame number since the stream opened,
 * channel 1 its complement, at the rate of its media clock (derived from
 * gPTP). The listener checks that every frame arrives, in order, and how
 * long before its presentation time it arrives.
 *
 * Run it in virtual time (--virtual) to stream without loss (decision 0013).
 */

#include <inttypes.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "genavb/genavb.h"

#include "paad/sim/console.h"

#include "audio.h"
#include "controlled.h"

#define SAMPLE_RATE      48000
#define CHANNELS         2
#define FRAME_BYTES      (CHANNELS * 4)
#define BATCH_FRAMES     48 /* 1 ms */
#define TASK_PRIORITY    (tskIDLE_PRIORITY + 2)
#define TASK_STACK       (configMINIMAL_STACK_SIZE * 2)

/* The format this application handles: AAF, 2 channels, 32/32 bits,
 * 48 kHz, 6 frames per packet (the entity's AAF streams). */
static const uint64_t aaf_format = 0x0205022000806000ULL;

struct stream {
	struct genavb_stream_handle *handle;
	uint16_t index; /* stream input or output index in the entity */
};

static struct {
	struct genavb_handle *genavb;
	struct genavb_control_handle *media;
	const char *tag;

	struct stream talker;
	uint64_t t0;    /* gPTP time of frame 0 */
	uint32_t frame; /* next frame to send */

	struct stream listener;
	bool started;
	uint32_t next; /* expected frame number */

	/* statistics, per report period */
	uint64_t sent;
	uint64_t short_writes;
	uint64_t received;
	uint64_t gaps;
	int32_t margin_min_ns; /* presentation time - arrival, smallest */
	int32_t margin_max_ns;
	bool margin_valid;
} audio;

static uint64_t gptp_now(void)
{
	uint64_t now = 0;

	genavb_clock_gettime64(GENAVB_CLOCK_GPTP_0_0, &now);

	return now;
}

static uint64_t be64(const void *p)
{
	const uint8_t *b = p;
	uint64_t v = 0;
	int i;

	for (i = 0; i < 8; i++)
		v = (v << 8) | b[i];

	return v;
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

/* --- Stream connections ------------------------------------------------------ */

static void stream_open(struct genavb_stream_params *params, uint16_t index)
{
	bool talker = params->direction == AVTP_DIRECTION_TALKER;
	struct stream *s = talker ? &audio.talker : &audio.listener;
	unsigned int batch = BATCH_FRAMES * FRAME_BYTES;
	struct genavb_stream_handle *handle;
	int rc;

	if (be64(&params->format) != aaf_format) {
		paad_sim_printf("%s: ATDECC: stream %s %u connected, format %016" PRIx64 " not handled\n", audio.tag,
				talker ? "output" : "input", index, be64(&params->format));
		return;
	}

	if (s->handle) {
		paad_sim_printf("%s: ATDECC: stream %s %u connected, but %u is already running\n", audio.tag,
				talker ? "output" : "input", index, s->index);
		return;
	}

	rc = genavb_stream_create(audio.genavb, &handle, params, &batch, 0);
	if (rc != GENAVB_SUCCESS) {
		paad_sim_printf("%s: ATDECC: stream %s %u: cannot open it: %s\n", audio.tag, talker ? "output" : "input",
				index, genavb_strerror(rc));
		return;
	}

	taskENTER_CRITICAL();
	s->handle = handle;
	s->index = index;
	if (talker) {
		audio.t0 = gptp_now();
		audio.frame = 0;
	} else {
		audio.started = false;
		audio.margin_valid = false;
	}
	taskEXIT_CRITICAL();

	paad_sim_printf("%s: ATDECC: stream %s %u connected: stream ID %016" PRIx64
			", to %02x:%02x:%02x:%02x:%02x:%02x%s\n",
			audio.tag, talker ? "output" : "input", index, be64(params->stream_id), params->dst_mac[0],
			params->dst_mac[1], params->dst_mac[2], params->dst_mac[3], params->dst_mac[4], params->dst_mac[5],
			(!talker && (params->flags & GENAVB_STREAM_FLAGS_MCR)) ? ", media clock source" : "");
}

static void stream_close(avtp_direction_t direction, uint16_t index)
{
	bool talker = direction == AVTP_DIRECTION_TALKER;
	struct stream *s = talker ? &audio.talker : &audio.listener;
	struct genavb_stream_handle *handle;

	if (!s->handle || s->index != index)
		return;

	taskENTER_CRITICAL();
	handle = s->handle;
	s->handle = NULL;
	taskEXIT_CRITICAL();

	genavb_stream_destroy(handle);

	paad_sim_printf("%s: ATDECC: stream %s %u disconnected\n", audio.tag, talker ? "output" : "input", index);
}

static void handle_media_stack(void)
{
	union genavb_media_stack_msg msg;
	genavb_msg_type_t type;
	unsigned int len = sizeof(msg);
	int rc;

	while (genavb_control_receive(audio.media, &type, &msg, &len) == GENAVB_SUCCESS) {
		switch (type) {
		case GENAVB_MSG_MEDIA_STACK_CONNECT:
			stream_open(&msg.media_stack_connect.stream_params, msg.media_stack_connect.stream_index);
			break;

		case GENAVB_MSG_MEDIA_STACK_DISCONNECT:
			stream_close(msg.media_stack_disconnect.direction, msg.media_stack_disconnect.stream_index);
			break;

		case GENAVB_MSG_MEDIA_SET_CLOCK_SOURCE:
			/* The entity's clock domain changed source: apply it. */
			rc = control(GENAVB_CTRL_CLOCK_DOMAIN, GENAVB_MSG_CLOCK_DOMAIN_SET_SOURCE,
				     &msg.media_stack_set_clock_source, sizeof(msg.media_stack_set_clock_source));
			paad_sim_printf("%s: ATDECC: clock domain %u follows %s%s\n", audio.tag,
					msg.media_stack_set_clock_source.domain - GENAVB_CLOCK_DOMAIN_0,
					msg.media_stack_set_clock_source.source_type == GENAVB_CLOCK_SOURCE_TYPE_INTERNAL ?
						"gPTP" : "a stream input",
					rc == GENAVB_SUCCESS ? "" : " (failed)");
			break;

		case GENAVB_MSG_MEDIA_STACK_BIND:
			paad_sim_printf("%s: ATDECC: stream input %u bound to %016" PRIx64 " output %u\n", audio.tag,
					msg.media_stack_bind.listener_stream_index, msg.media_stack_bind.talker_entity_id,
					msg.media_stack_bind.talker_stream_index);
			break;

		case GENAVB_MSG_MEDIA_STACK_UNBIND:
			paad_sim_printf("%s: ATDECC: stream input %u unbound\n", audio.tag,
					msg.media_stack_unbind.listener_stream_index);
			break;

		default:
			break;
		}

		len = sizeof(msg);
	}
}

/* --- Talker ------------------------------------------------------------------ */

static void talker_send(void)
{
	uint8_t buf[BATCH_FRAMES * FRAME_BYTES];
	/* Frames due by now on the gPTP-derived media clock. */
	uint64_t due = (gptp_now() - audio.t0) * SAMPLE_RATE / 1000000000ULL;

	while (due - audio.frame >= BATCH_FRAMES) {
		unsigned int i;
		int rc;

		for (i = 0; i < BATCH_FRAMES; i++) {
			uint32_t s[CHANNELS] = { audio.frame + i, ~(audio.frame + i) };

			memcpy(&buf[i * FRAME_BYTES], s, sizeof(s));
		}

		rc = genavb_stream_send(audio.talker.handle, buf, sizeof(buf), NULL, 0);
		if (rc < (int)sizeof(buf)) {
			audio.short_writes++; /* stack full: try again later */
			break;
		}

		audio.frame += BATCH_FRAMES;
		audio.sent += BATCH_FRAMES;
	}
}

/* --- Listener ---------------------------------------------------------------- */

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
		audio.received++;
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

static void listener_receive(void)
{
	static uint8_t buf[16 * BATCH_FRAMES * FRAME_BYTES];
	struct genavb_event ev[64];

	for (;;) {
		unsigned int n_ev = 64;
		uint32_t now = (uint32_t)gptp_now();
		int rc = genavb_stream_receive(audio.listener.handle, buf, sizeof(buf), ev, &n_ev);

		if (rc <= 0)
			break;

		listener_check(buf, (unsigned int)rc, ev, n_ev, now);
	}
}

/* --- Task and report ----------------------------------------------------------- */

static void audio_task(void *arg)
{
	(void)arg;

	for (;;) {
		handle_media_stack();
		controlled_poll();

		if (audio.talker.handle)
			talker_send();

		if (audio.listener.handle)
			listener_receive();

		vTaskDelay(pdMS_TO_TICKS(1));
	}
}

int audio_start(struct genavb_handle *genavb, const char *tag)
{
	int rc;

	audio.genavb = genavb;
	audio.tag = tag;

	/* Opening this channel also starts the ATDECC entity (entity.c). */
	rc = genavb_control_open(genavb, &audio.media, GENAVB_CTRL_AVDECC_MEDIA_STACK);
	if (rc != GENAVB_SUCCESS)
		return rc;

	if (xTaskCreate(audio_task, "audio", TASK_STACK, NULL, TASK_PRIORITY, NULL) != pdPASS)
		return -GENAVB_ERR_NO_MEMORY;

	return GENAVB_SUCCESS;
}

void audio_report(const char *tag, unsigned int t)
{
	taskENTER_CRITICAL();
	bool talking = audio.talker.handle != NULL, listening = audio.listener.handle != NULL;
	uint64_t sent = audio.sent, shorts = audio.short_writes, received = audio.received, gaps = audio.gaps;
	int32_t mmin = audio.margin_min_ns, mmax = audio.margin_max_ns;
	bool mvalid = audio.margin_valid;
	audio.sent = 0;
	audio.short_writes = 0;
	audio.received = 0;
	audio.gaps = 0;
	audio.margin_valid = false;
	taskEXIT_CRITICAL();

	if (talking)
		paad_sim_printf("%s: t=%us audio talker: sent %" PRIu64 " frames, %" PRIu64 " waits for the stack\n", tag, t,
				sent, shorts);

	if (listening && mvalid)
		paad_sim_printf("%s: t=%us audio listener: received %" PRIu64 " frames, %" PRIu64
				" gaps, presentation in %d..%d us\n",
				tag, t, received, gaps, mmin / 1000, mmax / 1000);
	else if (listening)
		paad_sim_printf("%s: t=%us audio listener: received %" PRIu64 " frames, %" PRIu64 " gaps\n", tag, t,
				received, gaps);

	if (!talking && !listening)
		paad_sim_printf("%s: t=%us audio: no stream connected\n", tag, t);
}
