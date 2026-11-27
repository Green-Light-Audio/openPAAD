/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* openpaad-sim ATDECC entities. */

#ifndef OPENPAAD_SIM_ENTITY_H
#define OPENPAAD_SIM_ENTITY_H

#include <stdbool.h>
#include <stdint.h>

#include "genavb/genavb.h"

/* Entity index 0 is the device, index 1 its controller (if any). */
#define ENTITY_DEVICE     0
#define ENTITY_CONTROLLER 1

/* The device's stream input carrying audio (AAF), and the clock source of
 * its clock domain 0 that follows it, in GenAVB's reference Milan entity. */
#define ENTITY_AUDIO_INPUT          0
#define ENTITY_AUDIO_OUTPUT         0
#define ENTITY_CLOCK_SOURCE_AUDIO_IN 2

/* Entity ID of entity index on a device whose port 0 has address mac. */
uint64_t entity_id(const uint8_t mac[6], unsigned int index);

/* Set GenAVB's ATDECC configuration: Milan mode, the device entity named
 * name, and a controller entity if asked. The device entity starts once the
 * application has opened its media stack channel (audio.c). */
int entity_configure(struct avdecc_config *cfg, const uint8_t mac[6], const char *name, bool controller);

#endif
