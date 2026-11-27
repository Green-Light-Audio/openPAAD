/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* openpaad-sim media clock report. */

#ifndef OPENPAAD_SIM_MEDIA_CLOCK_H
#define OPENPAAD_SIM_MEDIA_CLOCK_H

#include <stdint.h>

/* One line: GenAVB's recovery of the device's media clock, and how the
 * simulated audio clock really runs against gPTP time, measured at a
 * simulated instant (source_ns) whose gPTP time is gptp_ns. */
void media_clock_report(const char *tag, unsigned int t, uint64_t source_ns, uint64_t gptp_ns);

#endif
