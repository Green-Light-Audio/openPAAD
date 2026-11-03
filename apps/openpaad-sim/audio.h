/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* openpaad-sim audio demo: one AAF stream from device a to device b. */

#ifndef OPENPAAD_SIM_AUDIO_H
#define OPENPAAD_SIM_AUDIO_H

#include <stdint.h>

#include "genavb/genavb.h"

/* Talker: gPTP media clock, SRP talker declaration, a counting pattern. */
int audio_talker_start(struct genavb_handle *genavb, const uint8_t mac[6]);

/* Listener: SRP listener declaration, checks what arrives. */
int audio_listener_start(struct genavb_handle *genavb, const uint8_t talker_mac[6]);

/* One line of statistics for the last second, prefixed with tag and t. */
void audio_report(const char *tag, unsigned int t);

#endif
