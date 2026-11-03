/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* openpaad-sim: the ATDECC commands GenAVB leaves to the application
 * (dynamic audio mappings, START/STOP_STREAMING). */

#ifndef OPENPAAD_SIM_CONTROLLED_H
#define OPENPAAD_SIM_CONTROLLED_H

#include "genavb/genavb.h"

struct aem_desc_hdr;

/* Open GenAVB's CONTROLLED channel for the device entity whose model is
 * aem, with default mappings on its dynamic stream ports. */
int controlled_start(struct genavb_handle *genavb, struct aem_desc_hdr *aem, const char *tag);

/* Answer the commands received since the last call (from the audio task). */
void controlled_poll(void);

#endif
