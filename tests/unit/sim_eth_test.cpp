/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Simulated Ethernet ports joined by a virtual cable (hal/sim), in virtual
// time: serialisation, propagation, timestamps on each port's own clock,
// multicast filtering, and a gPTP-style peer delay measurement.

#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "paad/sim/eth.h"

namespace {

constexpr uint64_t kUs = 1000ULL;
constexpr uint64_t kMs = 1000000ULL;
constexpr uint64_t kSec = 1000000000ULL;
constexpr uint32_t kPropagationNs = 523; // ~100 m of cable plus PHYs

const uint8_t kMacA[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x0a };
const uint8_t kMacB[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x0b };
const uint8_t kPtpMcast[6] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e };
const uint8_t kStreamMcast[6] = { 0x91, 0xe0, 0xf0, 0x00, 0xfe, 0x01 };
const uint8_t kBroadcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

struct Events {
	std::vector<paad_hal_eth_event> list;

	static void cb(paad_hal_eth *, const paad_hal_eth_event *ev, void *arg)
	{
		static_cast<Events *>(arg)->list.push_back(*ev);
	}

	size_t count(paad_hal_eth_event_type type) const
	{
		size_t n = 0;
		for (const auto &e : list)
			n += e.type == type;
		return n;
	}

	const paad_hal_eth_event *last(paad_hal_eth_event_type type) const
	{
		for (auto it = list.rbegin(); it != list.rend(); ++it)
			if (it->type == type)
				return &*it;
		return nullptr;
	}
};

struct Rx {
	std::vector<uint8_t> data;
	uint64_t timestamp_ns;
};

class SimEthTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ASSERT_EQ(paad_sim_world_init(&world, PAAD_SIM_VIRTUAL_TIME), PAAD_HAL_OK);
		paad_sim_world_run_until(&world, 1 * kSec);

		// Two endpoints whose free-running clocks disagree in both offset
		// and rate, as real ones do before gPTP has converged.
		ASSERT_EQ(paad_sim_clock_init(&clock_a, paad_sim_world_now, &world, 100 * kSec, 0), PAAD_HAL_OK);
		ASSERT_EQ(paad_sim_clock_init(&clock_b, paad_sim_world_now, &world, 3 * kSec + 12345, 50000), PAAD_HAL_OK);

		ASSERT_EQ(paad_sim_eth_init(&a, &world, &clock_a, "a"), PAAD_HAL_OK);
		ASSERT_EQ(paad_sim_eth_init(&b, &world, &clock_b, "b"), PAAD_HAL_OK);
		a.hal.event_cb = Events::cb;
		a.hal.event_arg = &ev_a;
		b.hal.event_cb = Events::cb;
		b.hal.event_arg = &ev_b;
		ASSERT_EQ(paad_sim_eth_connect(&a, &b, kPropagationNs), PAAD_HAL_OK);
	}

	void TearDown() override
	{
		paad_sim_world_run_for(&world, kSec); // let every frame land (no leaks)
		paad_sim_eth_deinit(&a);
		paad_sim_eth_deinit(&b);
		paad_sim_clock_deinit(&clock_a);
		paad_sim_clock_deinit(&clock_b);
		paad_sim_world_deinit(&world);
	}

	void start_both()
	{
		ASSERT_EQ(paad_hal_eth_start(&a.hal, kMacA), PAAD_HAL_OK);
		ASSERT_EQ(paad_hal_eth_start(&b.hal, kMacB), PAAD_HAL_OK);
	}

	static std::vector<uint8_t> make_frame(const uint8_t dst[6], const uint8_t src[6], size_t len, uint8_t fill)
	{
		std::vector<uint8_t> f(len, fill);
		std::memcpy(&f[0], dst, 6);
		std::memcpy(&f[6], src, 6);
		f[12] = 0x88;
		f[13] = 0xf7; // gPTP ethertype; content does not matter here
		return f;
	}

	int send(paad_sim_eth *port, std::vector<uint8_t> &frame, uintptr_t cookie, unsigned int queue = 0)
	{
		paad_hal_eth_frame f{};
		f.data = frame.data();
		f.len = static_cast<uint16_t>(frame.size());
		f.flags = PAAD_HAL_ETH_F_TX_TIMESTAMP;
		f.cookie = cookie;
		return paad_hal_eth_transmit(&port->hal, queue, &f);
	}

	std::vector<Rx> drain(paad_sim_eth *port)
	{
		std::vector<Rx> out;
		uint8_t buf[PAAD_SIM_ETH_MAX_FRAME];
		paad_hal_eth_frame f{};
		f.data = buf;
		while (paad_hal_eth_receive(&port->hal, &f, sizeof(buf)) == 1) {
			EXPECT_TRUE(f.flags & PAAD_HAL_ETH_F_TIMESTAMP_VALID);
			out.push_back({ std::vector<uint8_t>(buf, buf + f.len), f.timestamp_ns });
		}
		return out;
	}

	paad_sim_world world{};
	paad_sim_clock clock_a{}, clock_b{};
	paad_sim_eth a{}, b{};
	Events ev_a, ev_b;
};

} // namespace

TEST(SimEthWire, WireTime)
{
	// 1500-byte payload frame: 1514 + 4 FCS + 8 preamble + 12 IPG = 1538 B
	EXPECT_EQ(paad_sim_eth_wire_time_ns(1514, 1000), 12304u);
	// Short frames are padded to the 64-byte minimum.
	EXPECT_EQ(paad_sim_eth_wire_time_ns(42, 1000), 672u);
	EXPECT_EQ(paad_sim_eth_wire_time_ns(42, 100), 6720u);
}

TEST_F(SimEthTest, LinkComesUpWhenBothEndsStart)
{
	paad_hal_eth_link link{};

	ASSERT_EQ(paad_hal_eth_start(&a.hal, kMacA), PAAD_HAL_OK);
	ASSERT_EQ(paad_hal_eth_get_link(&a.hal, &link), PAAD_HAL_OK);
	EXPECT_FALSE(link.up);
	EXPECT_EQ(ev_a.count(PAAD_HAL_ETH_EV_LINK), 0u);
	EXPECT_EQ(paad_hal_eth_start(&a.hal, kMacA), PAAD_HAL_ERR_STATE);

	ASSERT_EQ(paad_hal_eth_start(&b.hal, kMacB), PAAD_HAL_OK);
	ASSERT_EQ(paad_hal_eth_get_link(&a.hal, &link), PAAD_HAL_OK);
	EXPECT_TRUE(link.up);
	EXPECT_EQ(link.speed_mbps, 1000u);
	EXPECT_TRUE(link.full_duplex);
	EXPECT_EQ(ev_a.count(PAAD_HAL_ETH_EV_LINK), 1u);
	EXPECT_EQ(ev_b.count(PAAD_HAL_ETH_EV_LINK), 1u);

	paad_hal_eth_stop(&b.hal);
	ASSERT_EQ(paad_hal_eth_get_link(&a.hal, &link), PAAD_HAL_OK);
	EXPECT_FALSE(link.up);
	EXPECT_EQ(ev_a.count(PAAD_HAL_ETH_EV_LINK), 2u);
}

TEST_F(SimEthTest, CapsDescribeTwoShapedClasses)
{
	paad_hal_eth_caps caps{};

	ASSERT_EQ(paad_hal_eth_get_caps(&a.hal, &caps), PAAD_HAL_OK);
	EXPECT_EQ(caps.num_tx_queues, 3u);
	EXPECT_EQ(caps.num_shaped_queues, 2u);
	EXPECT_TRUE(caps.tx_timestamps);
	EXPECT_TRUE(caps.rx_timestamps);
	EXPECT_EQ(paad_hal_eth_set_shaper(&a.hal, 2, 6144000), PAAD_HAL_OK);
	EXPECT_EQ(paad_hal_eth_set_shaper(&a.hal, 0, 6144000), PAAD_HAL_ERR_INVALID); // best effort
}

TEST_F(SimEthTest, TransmitBeforeStartFails)
{
	auto f = make_frame(kMacB, kMacA, 60, 0);
	EXPECT_EQ(send(&a, f, 1), PAAD_HAL_ERR_STATE);
}

TEST_F(SimEthTest, UnicastFrameIsDeliveredAndTimestamped)
{
	start_both();
	auto f = make_frame(kMacB, kMacA, 100, 0x5a);
	uint64_t t_send = paad_sim_world_now(&world);

	ASSERT_EQ(send(&a, f, 0xc0ffee), PAAD_HAL_OK);
	EXPECT_TRUE(drain(&b).empty()); // nothing arrives in zero time

	paad_sim_world_run_for(&world, 10 * kUs);

	auto rx = drain(&b);
	ASSERT_EQ(rx.size(), 1u);
	EXPECT_EQ(rx[0].data, f);
	EXPECT_EQ(ev_b.count(PAAD_HAL_ETH_EV_RX_READY), 1u);

	const paad_hal_eth_event *done = ev_a.last(PAAD_HAL_ETH_EV_TX_COMPLETE);
	ASSERT_NE(done, nullptr);
	EXPECT_EQ(done->cookie, 0xc0ffeeu);
	ASSERT_TRUE(done->flags & PAAD_HAL_ETH_F_TIMESTAMP_VALID);

	// Each port timestamps on its own clock, at the same point of the frame.
	EXPECT_EQ(done->timestamp_ns, paad_sim_clock_at(&clock_a, t_send));
	EXPECT_EQ(rx[0].timestamp_ns, paad_sim_clock_at(&clock_b, t_send + kPropagationNs));
	EXPECT_EQ(a.stats.tx_frames, 1u);
	EXPECT_EQ(b.stats.rx_frames, 1u);
}

TEST_F(SimEthTest, FramesAreSerialisedAtLinkSpeed)
{
	start_both();
	std::vector<std::vector<uint8_t>> frames;
	for (int i = 0; i < 4; i++)
		frames.push_back(make_frame(kMacA, kMacB, 1514, static_cast<uint8_t>(i)));

	// Queued back to back from B to A (A's clock is the rate reference).
	for (size_t i = 0; i < frames.size(); i++)
		ASSERT_EQ(send(&b, frames[i], i), PAAD_HAL_OK);

	paad_sim_world_run_for(&world, kMs);

	auto rx = drain(&a);
	ASSERT_EQ(rx.size(), frames.size());
	for (size_t i = 0; i < rx.size(); i++) {
		EXPECT_EQ(rx[i].data, frames[i]); // in order
		if (i) {
			EXPECT_EQ(rx[i].timestamp_ns - rx[i - 1].timestamp_ns, 12304u);
		}
	}
	EXPECT_EQ(ev_b.count(PAAD_HAL_ETH_EV_TX_COMPLETE), frames.size());
}

TEST_F(SimEthTest, MulticastIsFiltered)
{
	start_both();
	const uint8_t other_unicast[6] = { 0x02, 0, 0, 0, 0, 0x99 };
	auto ptp = make_frame(kPtpMcast, kMacA, 60, 1);
	auto stream = make_frame(kStreamMcast, kMacA, 60, 2);
	auto bcast = make_frame(kBroadcast, kMacA, 60, 3);
	auto stray = make_frame(other_unicast, kMacA, 60, 4);

	ASSERT_EQ(send(&a, ptp, 1), PAAD_HAL_OK);
	ASSERT_EQ(send(&a, stream, 2), PAAD_HAL_OK);
	ASSERT_EQ(send(&a, bcast, 3), PAAD_HAL_OK);
	ASSERT_EQ(send(&a, stray, 4), PAAD_HAL_OK);
	paad_sim_world_run_for(&world, kMs);

	auto rx = drain(&b);
	ASSERT_EQ(rx.size(), 1u); // only broadcast without any subscription
	EXPECT_EQ(rx[0].data, bcast);
	EXPECT_EQ(b.stats.rx_dropped_filter, 3u);

	ASSERT_EQ(paad_hal_eth_set_multicast(&b.hal, kPtpMcast, true), PAAD_HAL_OK);
	ASSERT_EQ(paad_hal_eth_set_multicast(&b.hal, kStreamMcast, true), PAAD_HAL_OK);
	ASSERT_EQ(paad_hal_eth_set_multicast(&b.hal, kStreamMcast, true), PAAD_HAL_OK); // idempotent
	EXPECT_EQ(paad_hal_eth_set_multicast(&b.hal, kMacA, true), PAAD_HAL_ERR_INVALID); // not multicast

	ASSERT_EQ(send(&a, ptp, 5), PAAD_HAL_OK);
	ASSERT_EQ(send(&a, stream, 6), PAAD_HAL_OK);
	paad_sim_world_run_for(&world, kMs);
	EXPECT_EQ(drain(&b).size(), 2u);

	ASSERT_EQ(paad_hal_eth_set_multicast(&b.hal, kStreamMcast, false), PAAD_HAL_OK);
	ASSERT_EQ(send(&a, ptp, 7), PAAD_HAL_OK);
	ASSERT_EQ(send(&a, stream, 8), PAAD_HAL_OK);
	paad_sim_world_run_for(&world, kMs);
	rx = drain(&b);
	ASSERT_EQ(rx.size(), 1u);
	EXPECT_EQ(rx[0].data, ptp);
}

TEST_F(SimEthTest, FramesSentWithLinkDownAreCompletedButLost)
{
	ASSERT_EQ(paad_hal_eth_start(&a.hal, kMacA), PAAD_HAL_OK);
	auto f = make_frame(kMacB, kMacA, 60, 0);
	ASSERT_EQ(send(&a, f, 42), PAAD_HAL_OK);
	paad_sim_world_run_for(&world, kMs);

	EXPECT_EQ(ev_a.count(PAAD_HAL_ETH_EV_TX_COMPLETE), 1u);
	EXPECT_EQ(a.stats.tx_dropped_link_down, 1u);
}

// The measurement gPTP makes on every link (IEEE 802.1AS peer delay): with
// four timestamps from two unsynchronised clocks, and the neighbour rate
// ratio, the link delay comes out as the cable's propagation delay.
TEST_F(SimEthTest, PeerDelayMeasuresPropagation)
{
	start_both();
	ASSERT_EQ(paad_hal_eth_set_multicast(&a.hal, kPtpMcast, true), PAAD_HAL_OK);
	ASSERT_EQ(paad_hal_eth_set_multicast(&b.hal, kPtpMcast, true), PAAD_HAL_OK);

	auto req = make_frame(kPtpMcast, kMacA, 68, 0x02);
	auto resp = make_frame(kPtpMcast, kMacB, 68, 0x03);
	const double ratio = 1.0 + 50000e-9; // B's rate relative to A's

	ASSERT_EQ(send(&a, req, 1), PAAD_HAL_OK);
	paad_sim_world_run_for(&world, 10 * kUs);
	uint64_t t1 = ev_a.last(PAAD_HAL_ETH_EV_TX_COMPLETE)->timestamp_ns;
	auto rx_b = drain(&b);
	ASSERT_EQ(rx_b.size(), 1u);
	uint64_t t2 = rx_b[0].timestamp_ns;

	paad_sim_world_run_for(&world, 37 * kUs); // responder turnaround
	ASSERT_EQ(send(&b, resp, 2), PAAD_HAL_OK);
	paad_sim_world_run_for(&world, 10 * kUs);
	uint64_t t3 = ev_b.last(PAAD_HAL_ETH_EV_TX_COMPLETE)->timestamp_ns;
	auto rx_a = drain(&a);
	ASSERT_EQ(rx_a.size(), 1u);
	uint64_t t4 = rx_a[0].timestamp_ns;

	double delay = (static_cast<double>(t4 - t1) - static_cast<double>(t3 - t2) / ratio) / 2.0;
	EXPECT_NEAR(delay, kPropagationNs, 2.0);
}
