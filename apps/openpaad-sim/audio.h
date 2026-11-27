/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* openpaad-sim audio: the streams ATDECC connects. */

#ifndef OPENPAAD_SIM_AUDIO_H
#define OPENPAAD_SIM_AUDIO_H

#include "genavb/genavb.h"

/* Listen for stream connections and run the audio of connected streams. */
int audio_start(struct genavb_handle *genavb, const char *tag);

/* One line of statistics per connected stream for the last second (or one
 * saying none is connected), prefixed with tag and t. */
void audio_report(const char *tag, unsigned int t);

#endif
