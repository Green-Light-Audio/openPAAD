/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Thin C wrappers around GenAVB internals, so that C++ tests do not have to
 * include GenAVB's C-only headers.
 */

#include <string.h>

#include "genavb/avdecc.h"
#include "common/srp.h"

#include "genavb_shim.h"

unsigned int shim_genavb_tspec(const uint8_t stream_format[8], unsigned int *max_interval_frames)
{
	struct avdecc_format format;
	unsigned int max_frame_size = 0;

	memcpy(&format, stream_format, sizeof(format));
	avdecc_fmt_tspec(&format, SR_CLASS_A, &max_frame_size, max_interval_frames);

	return max_frame_size;
}

uint64_t shim_genavb_idle_slope(unsigned int max_frame_size, unsigned int max_interval_frames)
{
	return srp_tspec_to_idle_slope(0, SR_CLASS_A, max_frame_size, max_interval_frames);
}
