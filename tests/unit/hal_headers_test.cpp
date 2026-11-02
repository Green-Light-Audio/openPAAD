/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// The HAL headers compile as C++ and link with C code (requirement: ports
// and tools written in C++ can use them).

#include <gtest/gtest.h>

#include "paad/hal.h"

extern "C" int paad_hal_headers_c_version(void);

namespace {

int fake_set_shaper_calls = 0;

int fake_start(paad_hal_eth *, const uint8_t *) { return PAAD_HAL_OK; }
void fake_stop(paad_hal_eth *) {}
int fake_get_caps(paad_hal_eth *, paad_hal_eth_caps *caps)
{
	caps->num_tx_queues = 3;
	caps->num_shaped_queues = 2;
	return PAAD_HAL_OK;
}
int fake_get_link(paad_hal_eth *, paad_hal_eth_link *link)
{
	link->up = true;
	link->speed_mbps = 1000;
	link->full_duplex = true;
	return PAAD_HAL_OK;
}
int fake_set_multicast(paad_hal_eth *, const uint8_t *, bool) { return PAAD_HAL_OK; }
int fake_transmit(paad_hal_eth *, unsigned int, const paad_hal_eth_frame *) { return PAAD_HAL_OK; }
int fake_receive(paad_hal_eth *, paad_hal_eth_frame *, uint16_t) { return 0; }
int fake_set_shaper(paad_hal_eth *, unsigned int, uint64_t)
{
	fake_set_shaper_calls++;
	return PAAD_HAL_OK;
}

} // namespace

TEST(HalHeaders, SameVersionInCAndCpp)
{
	EXPECT_EQ(paad_hal_headers_c_version(), PAAD_HAL_VERSION_MAJOR * 100 + PAAD_HAL_VERSION_MINOR);
}

TEST(HalHeaders, WrappersDispatchToOps)
{
	paad_hal_eth_ops ops = {};
	ops.start = fake_start;
	ops.stop = fake_stop;
	ops.get_caps = fake_get_caps;
	ops.get_link = fake_get_link;
	ops.set_multicast = fake_set_multicast;
	ops.transmit = fake_transmit;
	ops.receive = fake_receive;
	ops.set_shaper = fake_set_shaper;

	paad_hal_eth eth = {};
	eth.ops = &ops;

	paad_hal_eth_caps caps = {};
	ASSERT_EQ(paad_hal_eth_get_caps(&eth, &caps), PAAD_HAL_OK);
	EXPECT_EQ(caps.num_shaped_queues, 2);

	paad_hal_eth_link link = {};
	ASSERT_EQ(paad_hal_eth_get_link(&eth, &link), PAAD_HAL_OK);
	EXPECT_TRUE(link.up);

	EXPECT_EQ(paad_hal_eth_set_shaper(&eth, 2, 5824000), PAAD_HAL_OK);
	EXPECT_EQ(fake_set_shaper_calls, 1);
}

TEST(HalHeaders, OptionalOpsReportNotSupported)
{
	paad_hal_eth_ops ops = {};
	paad_hal_eth eth = {};
	eth.ops = &ops;
	EXPECT_EQ(paad_hal_eth_set_shaper(&eth, 0, 1000), PAAD_HAL_ERR_NOT_SUPPORTED);

	paad_hal_timer_ops tops = {};
	paad_hal_timer timer = {};
	timer.ops = &tops;
	EXPECT_EQ(paad_hal_timer_set_output(&timer, true), PAAD_HAL_ERR_NOT_SUPPORTED);
}
