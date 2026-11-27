/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* C bridge to GenAVB's OS clock layer, for genavb_clock_test.cpp. */

#ifndef OPENPAAD_TESTS_GENAVB_CLOCK_FIXTURE_H
#define OPENPAAD_TESTS_GENAVB_CLOCK_FIXTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void gc_set_hw_time(uint64_t ns);
int gc_init(void);
int gc_set_gptp_freq(int32_t ppb);
int gc_monotonic(uint64_t *ns);
int gc_monotonic_to_hw(uint64_t ns, uint64_t *hw_ns);
int gc_monotonic_delta_to_hw(uint64_t delta_ns, uint64_t *hw_delta_ns);

#ifdef __cplusplus
}
#endif

#endif
