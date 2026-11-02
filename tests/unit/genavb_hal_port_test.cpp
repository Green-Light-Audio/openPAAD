/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// GenAVB network port driver on top of the HAL (paad/genavb), driven
// through GenAVB's own driver interface, over two simulated ports joined by
// a virtual cable, in virtual time. Runs inside FreeRTOS (rtos_test_main).

#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "FreeRTOS.h"
#include "task.h"

#include "genavb_hal_fixture.h"
#include "paad/genavb/hal_port.h"
#include "paad/sim/eth.h"

namespace {

constexpr uint64_t kUs = 1000ULL;
constexpr uint64_t kMs = 1000000ULL;
constexpr uint64_t kSec = 1000000000ULL;
constexpr uint32_t kPropagationNs = 500;
constexpr unsigned kTimersPerPort = 3;

const uint8_t kMac[2][6] = { { 0x02, 0, 0, 0, 0, 0x0a }, { 0x02, 0, 0, 0, 0, 0x0b } };
const uint8_t kPtpMcast[6] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e };

std::vector<uint8_t> frame_to(const uint8_t dst[6], const uint8_t src[6], size_t len, uint8_t fill)
{
	std::vector<uint8_t> f(len, fill);
	std::memcpy(&f[0], dst, 6);
	std::memcpy(&f[6], src, 6);
	f[12] = 0x88;
	f[13] = 0xf7;
	return f;
}

class GenavbHalPortTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		gh_reset_record();
		ASSERT_EQ(paad_sim_world_init(&world, PAAD_SIM_VIRTUAL_TIME), PAAD_HAL_OK);
		paad_sim_world_run_until(&world, kSec);
		ASSERT_EQ(paad_sim_clock_init(&clock[0], paad_sim_world_now, &world, 100 * kSec, 0), PAAD_HAL_OK);
		ASSERT_EQ(paad_sim_clock_init(&clock[1], paad_sim_world_now, &world, 3 * kSec, 50000), PAAD_HAL_OK);

		for (unsigned i = 0; i < 2; i++) {
			ASSERT_EQ(paad_sim_eth_init(&eth[i], &world, &clock[i], i ? "b" : "a"), PAAD_HAL_OK);
			eth_ptr[i] = &eth[i].hal;
			for (unsigned t = 0; t < kTimersPerPort; t++) {
				paad_sim_timer *st = &timers[i * kTimersPerPort + t];
				ASSERT_EQ(paad_sim_timer_init(st, &world, &clock[i]), PAAD_HAL_OK);
				timer_ptr[i * kTimersPerPort + t] = &st->hal;
			}
		}
		ASSERT_EQ(paad_sim_eth_connect(&eth[0], &eth[1], kPropagationNs), PAAD_HAL_OK);

		platform.name = "test";
		platform.eth = eth_ptr;
		platform.num_eth = 2;
		platform.timers = timer_ptr;
		platform.num_timers = 2 * kTimersPerPort;
		gh_set_platform(&platform);

		ASSERT_EQ(gh_init(0, kMac[0]), 0);
		ASSERT_EQ(gh_init(1, kMac[1]), 0);
	}

	void TearDown() override
	{
		paad_sim_world_run_for(&world, kSec); // let in-flight frames land
		gh_exit(0);
		gh_exit(1);
		for (auto &t : timers)
			paad_sim_timer_deinit(&t);
		for (auto &e : eth)
			paad_sim_eth_deinit(&e);
		for (auto &c : clock)
			paad_sim_clock_deinit(&c);
		paad_sim_world_deinit(&world);
	}

	uint64_t now_source() { return paad_sim_world_now(&world); }

	paad_sim_world world{};
	paad_sim_clock clock[2]{};
	paad_sim_eth eth[2]{};
	paad_sim_timer timers[2 * kTimersPerPort]{};
	paad_hal_eth *eth_ptr[2]{};
	paad_hal_timer *timer_ptr[2 * kTimersPerPort]{};
	paad_hal_platform platform{};
};

} // namespace

TEST_F(GenavbHalPortTest, QueuesFollowTheHalCapabilities)
{
	// Simulated port: 3 strict-priority queues, the top 2 with a shaper.
	ASSERT_EQ(gh_num_tx_queues(0), 3u);
	EXPECT_EQ(gh_queue_prop(0, 0), GH_TX_PROP_SP);
	EXPECT_EQ(gh_queue_prop(0, 1), GH_TX_PROP_SP | GH_TX_PROP_CBS);
	EXPECT_EQ(gh_queue_prop(0, 2), GH_TX_PROP_SP | GH_TX_PROP_CBS);
}

TEST_F(GenavbHalPortTest, ClocksAreRegistered)
{
	EXPECT_TRUE(gh_clock_registered(GH_CLOCK_PORT0));
	EXPECT_TRUE(gh_clock_registered(GH_CLOCK_PORT1));
	EXPECT_TRUE(gh_clock_registered(GH_CLOCK_MONOTONIC)); // provided by the first port
}

// GenAVB hardware time is the HAL clock, to the nanosecond.
TEST_F(GenavbHalPortTest, HardwareClockIsTheHalClock)
{
	EXPECT_EQ(gh_clock_read(GH_CLOCK_PORT0), paad_hal_clock_read_ns(&clock[0].hal));
	EXPECT_EQ(gh_clock_read(GH_CLOCK_PORT1), paad_hal_clock_read_ns(&clock[1].hal));
	EXPECT_EQ(gh_clock_read(GH_CLOCK_MONOTONIC), paad_hal_clock_read_ns(&clock[0].hal));

	uint64_t t = gh_clock_read(GH_CLOCK_PORT1);
	EXPECT_EQ(gh_clock_cycles_to_time(GH_CLOCK_PORT1, t + 1234), t + 1234);
	EXPECT_EQ(gh_clock_cycles_to_time(GH_CLOCK_PORT1, t - 1234), t - 1234); // slightly in the past

	// Frequency adjustments reach the HAL clock.
	ASSERT_EQ(gh_clock_setfreq(GH_CLOCK_PORT0, 100000), 0); // +100 ppm
	uint64_t before = gh_clock_read(GH_CLOCK_PORT0);
	paad_sim_world_run_for(&world, kSec);
	EXPECT_EQ(gh_clock_read(GH_CLOCK_PORT0) - before, kSec + 100000);

	// Beyond the clock's range the HAL clamps (simulated clock: 500 ppm).
	ASSERT_EQ(gh_clock_setfreq(GH_CLOCK_PORT0, 900000), 0);
	before = gh_clock_read(GH_CLOCK_PORT0);
	paad_sim_world_run_for(&world, kSec);
	EXPECT_EQ(gh_clock_read(GH_CLOCK_PORT0) - before, kSec + 500000);
}

TEST_F(GenavbHalPortTest, FrameRoundTripWithTimestamps)
{
	auto f = frame_to(kMac[1], kMac[0], 90, 0x5a);
	uint64_t t_send = now_source();

	ASSERT_EQ(gh_send(0, f.data(), static_cast<uint16_t>(f.size()), true, 0xabcd, 2), 0);
	paad_sim_world_run_for(&world, 100 * kUs);

	// Transmit side: completion handled in tx_cleanup (net tx task context).
	EXPECT_EQ(gh_get_record().ptp_tx_ts_calls, 0u);
	gh_tx_cleanup(0);
	auto rec = gh_get_record();
	EXPECT_EQ(rec.ptp_tx_ts_calls, 1u);
	EXPECT_EQ(rec.ptp_tx_ts_port, 0u);
	EXPECT_EQ(rec.ptp_tx_ts_priv, 0xabcdu);
	EXPECT_EQ(rec.ptp_tx_ts_ts, paad_sim_clock_at(&clock[0], t_send));
	EXPECT_EQ(rec.tx_freed, 1u);

	// Receive side.
	uint8_t buf[1600];
	uint16_t len = 0;
	uint64_t ts = 0;
	ASSERT_EQ(gh_receive(1, buf, &len, &ts), 1);
	EXPECT_EQ(std::vector<uint8_t>(buf, buf + len), f);
	EXPECT_EQ(ts, paad_sim_clock_at(&clock[1], t_send + kPropagationNs));
	EXPECT_EQ(gh_receive(1, buf, &len, &ts), 0);
}

TEST_F(GenavbHalPortTest, FramesWithoutTimestampAreJustFreed)
{
	auto f = frame_to(kMac[1], kMac[0], 60, 1);
	ASSERT_EQ(gh_send(0, f.data(), static_cast<uint16_t>(f.size()), false, 0, 0), 0);
	paad_sim_world_run_for(&world, 100 * kUs);
	gh_tx_cleanup(0);
	EXPECT_EQ(gh_get_record().ptp_tx_ts_calls, 0u);
	EXPECT_EQ(gh_get_record().tx_freed, 1u);
}

TEST_F(GenavbHalPortTest, MulticastSubscriptionsReachTheHal)
{
	auto f = frame_to(kPtpMcast, kMac[0], 68, 2);
	uint8_t buf[1600];
	uint16_t len;
	uint64_t ts;

	ASSERT_EQ(gh_send(0, f.data(), static_cast<uint16_t>(f.size()), false, 0, 0), 0);
	paad_sim_world_run_for(&world, 100 * kUs);
	EXPECT_EQ(gh_receive(1, buf, &len, &ts), 0);

	ASSERT_EQ(gh_add_multi(1, kPtpMcast), 0);
	ASSERT_EQ(gh_send(0, f.data(), static_cast<uint16_t>(f.size()), false, 0, 0), 0);
	paad_sim_world_run_for(&world, 100 * kUs);
	EXPECT_EQ(gh_receive(1, buf, &len, &ts), 1);

	ASSERT_EQ(gh_del_multi(1, kPtpMcast), 0);
	ASSERT_EQ(gh_send(0, f.data(), static_cast<uint16_t>(f.size()), false, 0, 0), 0);
	paad_sim_world_run_for(&world, 100 * kUs);
	EXPECT_EQ(gh_receive(1, buf, &len, &ts), 0);
	gh_tx_cleanup(0);
}

TEST_F(GenavbHalPortTest, IdleSlopeOnlyOnShapedQueues)
{
	EXPECT_EQ(gh_set_idle_slope(0, 6144000, 2), 0);
	EXPECT_EQ(eth[0].idle_slope_bps[2], 6144000u);
	EXPECT_EQ(gh_set_idle_slope(0, 1000000, 1), 0);
	EXPECT_EQ(eth[0].idle_slope_bps[1], 1000000u);
	EXPECT_EQ(gh_set_idle_slope(0, 1000000, 0), -1); // best effort queue
	EXPECT_EQ(gh_set_idle_slope(0, 1000000, 3), -1); // no such queue
}

namespace {
struct Fired {
	int count = 0;
	static void cb(void *arg) { static_cast<Fired *>(arg)->count++; }
};
} // namespace

TEST_F(GenavbHalPortTest, HardwareTimersRunOnThePortClock)
{
	Fired port1, mono;

	void *t1 = gh_timer_request(GH_CLOCK_PORT1, Fired::cb, &port1);
	void *tm = gh_timer_request(GH_CLOCK_MONOTONIC, Fired::cb, &mono);
	ASSERT_NE(t1, nullptr);
	ASSERT_NE(tm, nullptr);

	ASSERT_EQ(gh_timer_arm(t1, gh_clock_read(GH_CLOCK_PORT1) + 2 * kMs), 0);
	ASSERT_EQ(gh_timer_arm(tm, gh_clock_read(GH_CLOCK_MONOTONIC) + 5 * kMs), 0);

	paad_sim_world_run_for(&world, 1 * kMs);
	EXPECT_EQ(port1.count, 0);
	paad_sim_world_run_for(&world, 2 * kMs);
	EXPECT_EQ(port1.count, 1);
	EXPECT_EQ(mono.count, 0);
	paad_sim_world_run_for(&world, 3 * kMs);
	EXPECT_EQ(mono.count, 1);

	gh_timer_free(t1);
	gh_timer_free(tm);
}

TEST_F(GenavbHalPortTest, TransmitRingAppliesBackpressure)
{
	auto f = frame_to(kMac[1], kMac[0], 60, 3);
	int sent = 0;

	while (sent < 2 * PAAD_GENAVB_HAL_TX_RING &&
	       gh_send(0, f.data(), static_cast<uint16_t>(f.size()), false, 0, 0) == 0)
		sent++;
	EXPECT_EQ(sent, PAAD_GENAVB_HAL_TX_RING);
	EXPECT_EQ(paad_genavb_hal_stats(0)->tx_busy, 1u);

	paad_sim_world_run_for(&world, kMs);
	gh_tx_cleanup(0);
	EXPECT_EQ(gh_get_record().tx_freed, static_cast<unsigned>(PAAD_GENAVB_HAL_TX_RING));
	EXPECT_EQ(gh_send(0, f.data(), static_cast<uint16_t>(f.size()), false, 0, 0), 0);
}

TEST_F(GenavbHalPortTest, LinkChangesAreReportedOnce)
{
	bool fd;
	uint64_t rate;

	paad_genavb_hal_poll_link(); // both ports started in SetUp: link up
	auto rec = gh_get_record();
	EXPECT_EQ(rec.link_events, 2u);
	EXPECT_TRUE(rec.link_up);
	EXPECT_EQ(rec.link_speed, 1000u);
	EXPECT_EQ(rec.link_duplex, 1u);
	gh_status(0, &fd, &rate);
	EXPECT_TRUE(fd);
	EXPECT_EQ(rate, 1000000000u);

	paad_genavb_hal_poll_link();
	EXPECT_EQ(gh_get_record().link_events, 2u); // no change, no event

	paad_hal_eth_stop(&eth[1].hal); // the far end of A's cable goes down
	paad_genavb_hal_poll_link();
	rec = gh_get_record();
	EXPECT_EQ(rec.link_events, 4u); // both ends see the link down
	EXPECT_EQ(rec.link_port, 1u);
	EXPECT_FALSE(rec.link_up);
}

TEST_F(GenavbHalPortTest, LinkTaskReportsLinkUp)
{
	ASSERT_EQ(gh_post_init(0), 0);
	ASSERT_EQ(gh_post_init(1), 0); // one task for every HAL port

	for (int i = 0; i < 100 && gh_get_record().link_events < 2; i++)
		vTaskDelay(pdMS_TO_TICKS(10));

	auto rec = gh_get_record();
	EXPECT_EQ(rec.link_events, 2u);
	EXPECT_TRUE(rec.link_up);
}
