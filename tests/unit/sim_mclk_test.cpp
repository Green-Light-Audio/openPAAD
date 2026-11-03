/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Simulated media clock (hal/sim), in virtual time.

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "paad/sim/mclk.h"

namespace {

constexpr uint64_t kMs = 1000000ULL;
constexpr uint64_t kSec = 1000000000ULL;
constexpr uint32_t kTickHz = 24576000;

struct Captures {
	std::vector<paad_hal_mclk_sample> samples;
	std::vector<uint64_t> source_ns; // world time when delivered

	paad_sim_world *world = nullptr;

	static void cb(paad_hal_mclk *, const paad_hal_mclk_sample *s, void *arg)
	{
		auto *c = static_cast<Captures *>(arg);
		c->samples.push_back(*s);
		c->source_ns.push_back(paad_sim_world_now(c->world));
	}
};

class SimMclkTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ASSERT_EQ(paad_sim_world_init(&world, PAAD_SIM_VIRTUAL_TIME), PAAD_HAL_OK);
		// gPTP clock: arbitrary origin, 20 ppm fast
		ASSERT_EQ(paad_sim_clock_init(&clock, paad_sim_world_now, &world, 5 * kSec, 20000), PAAD_HAL_OK);
		// audio oscillator: 30 ppm slow
		ASSERT_EQ(paad_sim_mclk_init(&mclk, &world, &clock, kTickHz, 48000, -30000), PAAD_HAL_OK);
		cap.world = &world;
		mclk.hal.capture_cb = Captures::cb;
		mclk.hal.capture_arg = &cap;
	}

	void TearDown() override
	{
		paad_sim_mclk_deinit(&mclk);
		paad_sim_clock_deinit(&clock);
		paad_sim_world_deinit(&world);
	}

	// Count in ticks, with the fraction, as a double (fine for a test).
	double count(uint64_t source_ns)
	{
		paad_sim_mclk_count c = paad_sim_mclk_count_at(&mclk, source_ns);
		return (double)c.ticks + (double)c.frac / 4294967296.0;
	}

	paad_sim_world world{};
	paad_sim_clock clock{};
	paad_sim_mclk mclk{};
	Captures cap;
};

TEST_F(SimMclkTest, Describes)
{
	EXPECT_EQ(mclk.hal.tick_hz, kTickHz);
	EXPECT_EQ(mclk.hal.sample_rate, 48000u);
	EXPECT_EQ(mclk.hal.ptp_clock, &clock.hal);
	EXPECT_GT(mclk.hal.max_adjust_ppb, 250000); // GenAVB's recovery may ask for 250 ppm
}

TEST_F(SimMclkTest, RunsAtItsNaturalRate)
{
	const uint64_t t0 = paad_sim_world_now(&world);

	// 24.576 MHz x (1 - 30 ppm) = 24575262.72 ticks per second
	EXPECT_NEAR(count(t0 + kSec) - count(t0), 24575262.72, 1e-6);
	EXPECT_NEAR(count(t0 + 10 * kSec) - count(t0), 245752627.2, 1e-5);
	EXPECT_EQ(paad_sim_mclk_count_at(&mclk, t0).ticks, 0u); // starts at zero
}

TEST_F(SimMclkTest, AdjustIsAbsoluteAndContinuous)
{
	const uint64_t t0 = paad_sim_world_now(&world);
	paad_hal_ppb_t applied = 0;

	ASSERT_EQ(paad_sim_world_run_until(&world, t0 + kSec), PAAD_HAL_OK);
	const double before = count(t0 + kSec);

	// Cancel the natural error: back to nominal from now on.
	ASSERT_EQ(paad_hal_mclk_adjust(&mclk.hal, 30000, &applied), PAAD_HAL_OK);
	EXPECT_EQ(applied, 30000);
	EXPECT_NEAR(count(t0 + kSec), before, 1e-6); // no step
	EXPECT_NEAR(count(t0 + 2 * kSec) - count(t0 + kSec), 24576000.0, 1e-6);

	// Absolute, not cumulative: the same request changes nothing.
	ASSERT_EQ(paad_hal_mclk_adjust(&mclk.hal, 30000, nullptr), PAAD_HAL_OK);
	EXPECT_NEAR(count(t0 + 2 * kSec) - count(t0 + kSec), 24576000.0, 1e-6);
	EXPECT_EQ(paad_sim_mclk_adjustment(&mclk), 30000);
}

TEST_F(SimMclkTest, AdjustIsClamped)
{
	paad_hal_ppb_t applied = 0;

	ASSERT_EQ(paad_hal_mclk_adjust(&mclk.hal, 5000000, &applied), PAAD_HAL_OK);
	EXPECT_EQ(applied, PAAD_SIM_MCLK_MAX_ADJUST_PPB);
	ASSERT_EQ(paad_hal_mclk_adjust(&mclk.hal, -5000000, &applied), PAAD_HAL_OK);
	EXPECT_EQ(applied, -PAAD_SIM_MCLK_MAX_ADJUST_PPB);
}

TEST_F(SimMclkTest, CapturesAtTheGptpInstant)
{
	const uint64_t t0 = paad_sim_world_now(&world);
	const uint64_t at = paad_hal_clock_read_ns(&clock.hal) + 500 * kMs;

	ASSERT_EQ(paad_hal_mclk_request_capture(&mclk.hal, at), PAAD_HAL_OK);
	ASSERT_EQ(paad_sim_world_run_until(&world, t0 + kSec), PAAD_HAL_OK);

	ASSERT_EQ(cap.samples.size(), 1u);
	EXPECT_EQ(cap.samples[0].ptp_ns, at);
	// Delivered when the gPTP clock reached the instant (20 ppm fast: a bit
	// before 500 ms of source time), with the counter at that instant.
	const uint64_t source = paad_sim_clock_source_at(&clock, at);
	EXPECT_EQ(cap.source_ns[0], source);
	EXPECT_EQ(cap.samples[0].mclk_ticks, paad_sim_mclk_count_at(&mclk, source).ticks);
	EXPECT_EQ(mclk.captures, 1u);
}

TEST_F(SimMclkTest, OneCaptureAtATime)
{
	const uint64_t now = paad_hal_clock_read_ns(&clock.hal);

	EXPECT_EQ(paad_hal_mclk_request_capture(&mclk.hal, now), PAAD_HAL_ERR_INVALID); // already passed
	ASSERT_EQ(paad_hal_mclk_request_capture(&mclk.hal, now + 10 * kMs), PAAD_HAL_OK);
	EXPECT_EQ(paad_hal_mclk_request_capture(&mclk.hal, now + 20 * kMs), PAAD_HAL_ERR_BUSY);

	ASSERT_EQ(paad_sim_world_run_for(&world, 15 * kMs), PAAD_HAL_OK);
	ASSERT_EQ(cap.samples.size(), 1u);
	EXPECT_EQ(paad_hal_mclk_request_capture(&mclk.hal, now + 20 * kMs), PAAD_HAL_OK);
	ASSERT_EQ(paad_sim_world_run_for(&world, 15 * kMs), PAAD_HAL_OK);
	ASSERT_EQ(cap.samples.size(), 2u);
}

TEST_F(SimMclkTest, CaptureFollowsTheGptpClock)
{
	const uint64_t at = paad_hal_clock_read_ns(&clock.hal) + kSec;

	ASSERT_EQ(paad_hal_mclk_request_capture(&mclk.hal, at), PAAD_HAL_OK);
	// gPTP slows its clock down after the request: the instant comes later
	// in source time, and the capture with it.
	ASSERT_EQ(paad_hal_clock_adjust_frequency(&clock.hal, -400000), PAAD_HAL_OK);
	ASSERT_EQ(paad_sim_world_run_for(&world, 2 * kSec), PAAD_HAL_OK);

	ASSERT_EQ(cap.samples.size(), 1u);
	const uint64_t source = paad_sim_clock_source_at(&clock, at);
	EXPECT_EQ(cap.source_ns[0], source);
	EXPECT_EQ(paad_sim_clock_at(&clock, source), at);
	EXPECT_EQ(cap.samples[0].mclk_ticks, paad_sim_mclk_count_at(&mclk, source).ticks);
}

TEST_F(SimMclkTest, CapturesMeasureTheRate)
{
	// What media clock recovery does: captures one gPTP second apart give
	// the audio clock's rate in gPTP time. gPTP runs 20 ppm fast and the
	// oscillator 30 ppm slow: about 50 ppm fewer ticks per gPTP second.
	const uint64_t start = paad_hal_clock_read_ns(&clock.hal) + 10 * kMs;

	for (int i = 0; i < 3; i++) {
		ASSERT_EQ(paad_hal_mclk_request_capture(&mclk.hal, start + (uint64_t)i * kSec), PAAD_HAL_OK);
		ASSERT_EQ(paad_sim_world_run_for(&world, kSec), PAAD_HAL_OK);
	}
	ASSERT_EQ(cap.samples.size(), 3u);

	const double per_s = (double)(cap.samples[2].mclk_ticks - cap.samples[0].mclk_ticks) / 2.0;
	const double expected = kTickHz * (1.0 - 30e-6) / (1.0 + 20e-6);
	EXPECT_NEAR(per_s, expected, 1.0);
}

} // namespace
