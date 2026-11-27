/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * openpaad-sim: the ATDECC commands GenAVB leaves to the application.
 *
 * GenAVB answers most AEM commands itself, from the entity model. A few it
 * passes on, through its CONTROLLED channel, to the application that owns
 * the audio: the dynamic audio mappings of the stream ports (GET_AUDIO_MAP,
 * ADD_AUDIO_MAPPINGS, REMOVE_AUDIO_MAPPINGS), START/STOP_STREAMING and
 * SET_CONTROL. With no application listening, it answers them
 * ENTITY_MISBEHAVING, and a controller such as Hive then flags the whole
 * entity as misbehaving.
 *
 * This module keeps the mappings of every stream port that has no static
 * maps, checks each one against the entity model (stream, cluster and
 * channel must exist), and answers the rest as the device's audio path
 * would: streaming starts and stops with the stream connections anyway.
 * The simulated audio itself does not use the mappings.
 */

#include <string.h>

#include "genavb/aem.h"
#include "genavb/aem_helpers.h"
#include "genavb/aecp.h"
#include "genavb/genavb.h"

#include "paad/sim/console.h"

#include "controlled.h"

#define MAX_PORTS    4  /* stream port inputs and outputs, each */
#define MAX_MAPPINGS 32 /* per stream port */

struct mapping {
	uint16_t stream_index;
	uint16_t stream_channel;
	uint16_t cluster_offset;
	uint16_t cluster_channel;
};

struct port {
	bool dynamic; /* no static maps: the mappings are ours to keep */
	unsigned int count;
	struct mapping map[MAX_MAPPINGS];
};

static struct {
	struct genavb_control_handle *handle;
	struct aem_desc_hdr *aem;
	const char *tag;
	struct port in[MAX_PORTS];
	struct port out[MAX_PORTS];
} ctl;

static struct port *port_of(uint16_t type, uint16_t index)
{
	if (index >= MAX_PORTS)
		return NULL;
	if (type == AEM_DESC_TYPE_STREAM_PORT_INPUT)
		return &ctl.in[index];
	if (type == AEM_DESC_TYPE_STREAM_PORT_OUTPUT)
		return &ctl.out[index];

	return NULL;
}

/* Does the mapping name a stream, cluster and channel the entity has? */
static bool mapping_valid(uint16_t type, const struct stream_port_descriptor *port, const struct mapping *m)
{
	uint16_t streams = (uint16_t)aem_get_descriptor_max(ctl.aem, type == AEM_DESC_TYPE_STREAM_PORT_INPUT ?
								AEM_DESC_TYPE_STREAM_INPUT : AEM_DESC_TYPE_STREAM_OUTPUT);
	const struct audio_cluster_descriptor *cluster;

	if (m->stream_index >= streams)
		return false;
	if (m->cluster_offset >= ntohs(port->number_of_clusters))
		return false;

	cluster = aem_get_descriptor(ctl.aem, AEM_DESC_TYPE_AUDIO_CLUSTER,
				     (uint16_t)(ntohs(port->base_cluster) + m->cluster_offset), NULL);

	return cluster && m->cluster_channel < ntohs(cluster->channel_count);
}

static void mapping_read(const struct aecp_aem_get_audio_map_mappings_format *f, struct mapping *m)
{
	m->stream_index = ntohs(f->mapping_stream_index);
	m->stream_channel = ntohs(f->mapping_stream_channel);
	m->cluster_offset = ntohs(f->mapping_cluster_offset);
	m->cluster_channel = ntohs(f->mapping_cluster_channel);
}

static bool same_target(uint16_t type, const struct mapping *a, const struct mapping *b)
{
	/* A cluster channel of an input port plays one stream channel; a
	 * stream channel of an output port carries one cluster channel. */
	if (type == AEM_DESC_TYPE_STREAM_PORT_INPUT)
		return a->cluster_offset == b->cluster_offset && a->cluster_channel == b->cluster_channel;

	return a->stream_index == b->stream_index && a->stream_channel == b->stream_channel;
}

static void port_add(uint16_t type, struct port *p, const struct mapping *m)
{
	unsigned int i;

	for (i = 0; i < p->count; i++)
		if (same_target(type, &p->map[i], m)) {
			p->map[i] = *m;
			return;
		}

	p->map[p->count++] = *m;
}

static uint8_t modify_mappings(struct aecp_aem_pdu *pdu, bool add)
{
	struct aecp_aem_modify_audio_mappings_cmd_pdu *cmd = (void *)(pdu + 1);
	const struct aecp_aem_get_audio_map_mappings_format *f = (const void *)(cmd + 1);
	uint16_t type = ntohs(cmd->descriptor_type), index = ntohs(cmd->descriptor_index);
	uint16_t n = ntohs(cmd->number_of_mappings);
	const struct stream_port_descriptor *desc = aem_get_descriptor(ctl.aem, type, index, NULL);
	struct port *p = port_of(type, index);
	struct mapping m;
	unsigned int i, j, added = 0;

	if (!desc || !p)
		return AECP_AEM_NO_SUCH_DESCRIPTOR;
	if (!p->dynamic || n > (AVB_AECP_MAX_MSG_SIZE - sizeof(*pdu) - sizeof(*cmd)) / sizeof(*f))
		return AECP_AEM_BAD_ARGUMENTS;

	/* All or nothing: check every mapping first. */
	for (i = 0; i < n; i++) {
		mapping_read(&f[i], &m);
		if (!mapping_valid(type, desc, &m))
			return AECP_AEM_BAD_ARGUMENTS;
		if (add) {
			bool replaces = false;

			for (j = 0; j < p->count; j++)
				replaces = replaces || same_target(type, &p->map[j], &m);
			added += !replaces;
		}
	}
	if (add && p->count + added > MAX_MAPPINGS)
		return AECP_AEM_NO_RESOURCES;

	for (i = 0; i < n; i++) {
		mapping_read(&f[i], &m);
		if (add) {
			port_add(type, p, &m);
			continue;
		}
		for (j = 0; j < p->count; j++)
			if (!memcmp(&p->map[j], &m, sizeof(m))) {
				p->map[j] = p->map[--p->count];
				break;
			}
	}

	return AECP_AEM_SUCCESS;
}

static uint8_t get_audio_map(struct genavb_aecp_msg *msg)
{
	struct aecp_aem_pdu *pdu = (void *)msg->buf;
	struct aecp_aem_get_audio_map_rsp_pdu *rsp = (void *)(pdu + 1);
	struct aecp_aem_get_audio_map_mappings_format *f = (void *)(rsp + 1);
	uint16_t type = ntohs(rsp->descriptor_type), index = ntohs(rsp->descriptor_index);
	struct port *p = port_of(type, index);
	unsigned int i;

	rsp->number_of_maps = htons(1);
	rsp->number_of_mappings = htons(0);
	rsp->reserved = 0;
	msg->len = sizeof(*pdu) + sizeof(*rsp);

	if (!p || !aem_get_descriptor(ctl.aem, type, index, NULL))
		return AECP_AEM_NO_SUCH_DESCRIPTOR;
	if (ntohs(rsp->map_index) != 0)
		return AECP_AEM_BAD_ARGUMENTS;

	for (i = 0; i < p->count; i++) {
		f[i].mapping_stream_index = htons(p->map[i].stream_index);
		f[i].mapping_stream_channel = htons(p->map[i].stream_channel);
		f[i].mapping_cluster_offset = htons(p->map[i].cluster_offset);
		f[i].mapping_cluster_channel = htons(p->map[i].cluster_channel);
	}
	rsp->number_of_mappings = htons((uint16_t)p->count);
	msg->len += p->count * sizeof(*f);

	return AECP_AEM_SUCCESS;
}

void controlled_poll(void)
{
	union genavb_controlled_msg msg;
	genavb_msg_type_t type;
	unsigned int len = sizeof(msg);

	if (!ctl.handle)
		return;

	while (genavb_control_receive(ctl.handle, &type, &msg, &len) == GENAVB_SUCCESS) {
		struct aecp_aem_pdu *pdu = (void *)msg.aecp.buf;
		uint8_t status;

		len = sizeof(msg);
		if (type != GENAVB_MSG_AECP || msg.aecp.msg_type != AECP_AEM_COMMAND)
			continue;

		switch (AECP_AEM_GET_CMD_TYPE(pdu)) {
		case AECP_AEM_CMD_GET_AUDIO_MAP:
			status = get_audio_map(&msg.aecp);
			break;
		case AECP_AEM_CMD_ADD_AUDIO_MAPPINGS:
			status = modify_mappings(pdu, true);
			break;
		case AECP_AEM_CMD_REMOVE_AUDIO_MAPPINGS:
			status = modify_mappings(pdu, false);
			break;
		case AECP_AEM_CMD_START_STREAMING:
		case AECP_AEM_CMD_STOP_STREAMING:
			status = AECP_AEM_SUCCESS;
			break;
		default:
			status = AECP_AEM_NOT_IMPLEMENTED;
			break;
		}

		msg.aecp.msg_type = AECP_AEM_RESPONSE;
		msg.aecp.status = status;
		if (genavb_control_send(ctl.handle, GENAVB_MSG_AECP, &msg, sizeof(msg.aecp)) != GENAVB_SUCCESS)
			paad_sim_printf("%s: ATDECC: could not answer AEM command 0x%04x\n", ctl.tag,
					AECP_AEM_GET_CMD_TYPE(pdu));
	}
}

/* Default mappings: stream channel i to cluster i, channel 0. */
static void port_init(uint16_t type, uint16_t index)
{
	const struct stream_port_descriptor *desc = aem_get_descriptor(ctl.aem, type, index, NULL);
	struct port *p = port_of(type, index);
	uint16_t i;

	if (!desc || !p || ntohs(desc->number_of_maps) != 0)
		return;

	p->dynamic = true;
	for (i = 0; i < ntohs(desc->number_of_clusters) && p->count < MAX_MAPPINGS; i++) {
		struct mapping m = { .stream_index = 0, .stream_channel = i, .cluster_offset = i, .cluster_channel = 0 };

		if (mapping_valid(type, desc, &m))
			port_add(type, p, &m);
	}
}

int controlled_start(struct genavb_handle *genavb, struct aem_desc_hdr *aem, const char *tag)
{
	uint16_t i;

	ctl.aem = aem;
	ctl.tag = tag;
	for (i = 0; i < MAX_PORTS; i++) {
		port_init(AEM_DESC_TYPE_STREAM_PORT_INPUT, i);
		port_init(AEM_DESC_TYPE_STREAM_PORT_OUTPUT, i);
	}

	return genavb_control_open(genavb, &ctl.handle, GENAVB_CTRL_AVDECC_CONTROLLED);
}
