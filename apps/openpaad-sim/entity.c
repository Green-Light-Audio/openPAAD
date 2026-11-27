/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * openpaad-sim ATDECC entities.
 *
 * Every simulated device is a Milan-mode entity built from GenAVB's
 * reference listener and talker model: one AAF stream (2 channels, 32 bits,
 * 48 kHz) and one CRF stream in each direction, one clock domain whose
 * source is internal or one of the stream inputs. GenAVB's ATDECC stack
 * runs it: it answers controllers, binds stream inputs, reserves bandwidth
 * with SRP, and tells the application which streams to open.
 *
 * Entity IDs: the EUI-64 of port 0's MAC address (FF-FE inserted in the
 * middle) for the device, FF-FD for its controller. The simulated MAC
 * addresses are locally administered, and so are these IDs.
 */

#include <string.h>

#include "genavb/aem.h"
#include "genavb/aem_helpers.h"

#include "aem_manager_rtos.h"

#include "entity.h"

/* GenAVB's reference entity models (apps/common/aem-manager). */
void listener_talker_audio_single_milan_init(struct aem_desc_hdr *aem_desc);
void controller_init(struct aem_desc_hdr *aem_desc);

uint64_t entity_id(const uint8_t mac[6], unsigned int index)
{
	const uint8_t b[8] = { mac[0], mac[1], mac[2], 0xff, (uint8_t)(0xfe - index), mac[3], mac[4], mac[5] };
	uint64_t id = 0;
	unsigned int i;

	for (i = 0; i < 8; i++)
		id = (id << 8) | b[i];

	return id;
}

/* Build an entity from its reference model, in memory the stack can keep. */
static struct aem_desc_hdr *entity_load(void (*init)(struct aem_desc_hdr *aem_desc))
{
	struct aem_desc_hdr ref[AEM_NUM_DESC_TYPES];

	memset(ref, 0, sizeof(ref));
	if (aem_entity_create(ref, init) < 0)
		return NULL;

	return aem_entity_load_from_reference_entity(ref);
}

int entity_configure(struct avdecc_config *cfg, const uint8_t mac[6], const char *name, bool controller)
{
	struct avdecc_entity_config *dev = &cfg->entity_cfg[ENTITY_DEVICE];
	struct entity_descriptor *desc;
	struct aem_desc_hdr *aem;

	aem = entity_load(listener_talker_audio_single_milan_init);
	if (!aem)
		return -1;

	desc = aem[AEM_DESC_TYPE_ENTITY].ptr;
	memset(desc->entity_name, 0, sizeof(desc->entity_name));
	strncpy((char *)desc->entity_name, name, sizeof(desc->entity_name) - 1);

	cfg->milan_mode = true;
	cfg->num_entities = 1;
	dev->aem = aem;
	dev->entity_id = entity_id(mac, ENTITY_DEVICE);
	/* Start once the application listens for stream connections (it
	 * then also gets the clock domain's initial source) and for the
	 * commands GenAVB leaves to it (controlled.c). */
	dev->channel_waitmask = AVDECC_WAITMASK_MEDIA_STACK | AVDECC_WAITMASK_CONTROLLED;
	/* Milan fixes the advertisement's valid time at 20 s (10 in the ADPDU
	 * field). GenAVB's Linux build forces it in Milan mode
	 * (CFG_ADP_MILAN_VALID_TIME); its RTOS defaults use 62 s. */
	dev->valid_time = 20;

	if (controller) {
		struct avdecc_entity_config *ctl = &cfg->entity_cfg[ENTITY_CONTROLLER];

		aem = entity_load(controller_init);
		if (!aem)
			return -1;

		cfg->num_entities = 2;
		ctl->aem = aem;
		ctl->entity_id = entity_id(mac, ENTITY_CONTROLLER);
		ctl->channel_waitmask = 0;
	}

	return 0;
}
