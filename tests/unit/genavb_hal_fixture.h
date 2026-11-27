/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* C bridge between the GenAVB HAL port driver (paad/genavb) and the C++
 * tests: GenAVB's OS-layer headers are C only, so the tests drive the
 * driver through these functions. Also records the calls the driver makes
 * into GenAVB (stubbed here). */

#ifndef OPENPAAD_TESTS_GENAVB_HAL_FIXTURE_H
#define OPENPAAD_TESTS_GENAVB_HAL_FIXTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "paad/hal/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { GH_CLOCK_MONOTONIC = 1, GH_CLOCK_PORT0 = 3, GH_CLOCK_PORT1 = 4 };

#define GH_TX_PROP_SP  1u /* GenAVB TX_QUEUE_FLAGS_STRICT_PRIORITY */
#define GH_TX_PROP_CBS 2u /* GenAVB TX_QUEUE_FLAGS_CREDIT_SHAPER */

struct gh_record {
	unsigned int ptp_tx_ts_calls;
	unsigned int ptp_tx_ts_port;
	uint64_t ptp_tx_ts_ts;
	uint32_t ptp_tx_ts_priv;
	unsigned int tx_freed;
	unsigned int link_events;
	unsigned int link_port;
	bool link_up;
	unsigned int link_speed;
	unsigned int link_duplex;
};

void gh_global_init(void);                 /* once per process */
void gh_set_platform(const struct paad_hal_platform *pf);
void gh_reset_record(void);
struct gh_record gh_get_record(void);

int gh_init(unsigned int i, const uint8_t mac[6]); /* GenAVB port i on HAL port i */
int gh_post_init(unsigned int i);
void gh_exit(unsigned int i);

unsigned int gh_num_tx_queues(unsigned int i);
unsigned int gh_queue_prop(unsigned int i, unsigned int queue);

int gh_send(unsigned int i, const uint8_t *data, uint16_t len, bool timestamp, uint32_t priv, unsigned int queue);
void gh_tx_cleanup(unsigned int i);
/* 1: frame copied to buf, 0: none, <0: error */
int gh_receive(unsigned int i, uint8_t *buf, uint16_t *len, uint64_t *ts);

int gh_add_multi(unsigned int i, const uint8_t addr[6]);
int gh_del_multi(unsigned int i, const uint8_t addr[6]);
int gh_set_idle_slope(unsigned int i, uint64_t bps, unsigned int queue);
void gh_status(unsigned int i, bool *full_duplex, uint64_t *rate);

bool gh_clock_registered(unsigned int hw_clock_id);
uint64_t gh_clock_read(unsigned int hw_clock_id);
int gh_clock_setfreq(unsigned int hw_clock_id, int32_t ppb);
uint64_t gh_clock_cycles_to_time(unsigned int hw_clock_id, uint64_t cycles);

void *gh_timer_request(unsigned int hw_clock_id, void (*fn)(void *), void *arg);
int gh_timer_arm(void *timer, uint64_t cycles);
void gh_timer_free(void *timer);

#ifdef __cplusplus
}
#endif

#endif
