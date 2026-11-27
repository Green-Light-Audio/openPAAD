/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* openpaad-sim scripted ATDECC controller. */

#ifndef OPENPAAD_SIM_CONTROLLER_H
#define OPENPAAD_SIM_CONTROLLER_H

#include <stdint.h>

#include "genavb/genavb.h"

#include "paad/sim/world.h"

/* Check and keep a script: comma-separated actions at simulated seconds,
 * "connect@2,disconnect@10,connect@14". Returns -1 if it is malformed. */
int controller_parse(const char *script);

/* Run the script from the controller entity of this device, start_ns being
 * the world time of t=0: connect or disconnect listener's audio input to
 * talker's audio output (entity IDs). */
int controller_start(struct genavb_handle *genavb, struct paad_sim_world *world, uint64_t start_ns, const char *tag,
		     uint64_t talker, uint64_t listener);

#endif
