/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef OPENPAAD_TESTS_GENAVB_SHIM_H
#define OPENPAAD_TESTS_GENAVB_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tspec GenAVB declares for a stream format (8 bytes, network order), class A. */
unsigned int shim_genavb_tspec(const uint8_t stream_format[8], unsigned int *max_interval_frames);

/* Credit-based shaper idle slope GenAVB configures for a Tspec, in bit/s. */
uint64_t shim_genavb_idle_slope(unsigned int max_frame_size, unsigned int max_interval_frames);

#ifdef __cplusplus
}
#endif

#endif
