/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Simulated gPTP clock (hal/sim). Driven by a fake time source so every
// test is exact and instantaneous.

#include <cstdint>

#include <gtest/gtest.h>

#include "paad/sim/clock.h"

namespace {

constexpr uint64_t kSec = 1000000000ULL;

struct FakeTime {
	uint64_t now = 1000 * kSec;
	static uint64_t read(void *ctx) { return static_cast<FakeTime *>(ctx)->now; }
};

class SimClockTest : public ::testing::Test {
protected:
	void SetUp() override { ASSERT_EQ(paad_sim_clock_init(&clk, FakeTime::read, &time, 5 * kSec, 0), PAAD_HAL_OK); }
	void TearDown() override { paad_sim_clock_deinit(&clk); }

	uint64_t read() { return paad_hal_clock_read_ns(&clk.hal); }

	FakeTime time;
	paad_sim_clock clk{};
};

} // namespace

TEST_F(SimClockTest, StartsAtInitialValueAndFollowsSource)
{
	EXPECT_EQ(read(), 5 * kSec);
	time.now += kSec;
	EXPECT_EQ(read(), 6 * kSec);
}

TEST_F(SimClockTest, FrequencyAdjustmentChangesRate)
{
	ASSERT_EQ(paad_hal_clock_adjust_frequency(&clk.hal, 100000), PAAD_HAL_OK); // +100 ppm
	time.now += kSec;
	EXPECT_EQ(read(), 5 * kSec + kSec + 100000); // 100 us more after 1 s
}

TEST_F(SimClockTest, RateChangeIsContinuous)
{
	time.now += kSec;
	ASSERT_EQ(paad_hal_clock_adjust_frequency(&clk.hal, 200000), PAAD_HAL_OK);
	EXPECT_EQ(read(), 6 * kSec); // no jump at the change
	time.now += kSec;
	EXPECT_EQ(read(), 7 * kSec + 200000);
	ASSERT_EQ(paad_hal_clock_adjust_frequency(&clk.hal, 0), PAAD_HAL_OK);
	time.now += kSec;
	EXPECT_EQ(read(), 8 * kSec + 200000);
}

TEST_F(SimClockTest, AdjustmentIsAbsoluteNotCumulative)
{
	ASSERT_EQ(paad_hal_clock_adjust_frequency(&clk.hal, 50000), PAAD_HAL_OK);
	ASSERT_EQ(paad_hal_clock_adjust_frequency(&clk.hal, 50000), PAAD_HAL_OK);
	time.now += kSec;
	EXPECT_EQ(read(), 6 * kSec + 50000);
}

TEST_F(SimClockTest, AdjustmentIsClamped)
{
	ASSERT_EQ(paad_hal_clock_adjust_frequency(&clk.hal, 10000000), PAAD_HAL_OK); // 10000 ppm requested
	time.now += kSec;
	EXPECT_EQ(read(), 6 * kSec + static_cast<uint64_t>(clk.hal.max_adjust_ppb));
}

TEST_F(SimClockTest, StepMovesTimeBothWays)
{
	ASSERT_EQ(paad_hal_clock_step(&clk.hal, 1500), PAAD_HAL_OK);
	EXPECT_EQ(read(), 5 * kSec + 1500);
	ASSERT_EQ(paad_hal_clock_step(&clk.hal, -2500), PAAD_HAL_OK);
	EXPECT_EQ(read(), 5 * kSec - 1000);
	EXPECT_EQ(paad_hal_clock_step(&clk.hal, -static_cast<int64_t>(10 * kSec)), PAAD_HAL_ERR_INVALID);
	EXPECT_EQ(read(), 5 * kSec - 1000); // refused step changes nothing
}

TEST(SimClock, NaturalErrorMakesTwoClocksDriftApart)
{
	FakeTime time;
	paad_sim_clock a{}, b{};
	ASSERT_EQ(paad_sim_clock_init(&a, FakeTime::read, &time, 0, 20000), PAAD_HAL_OK);  // +20 ppm crystal
	ASSERT_EQ(paad_sim_clock_init(&b, FakeTime::read, &time, 0, -15000), PAAD_HAL_OK); // -15 ppm crystal

	time.now += 10 * kSec;
	const int64_t drift = static_cast<int64_t>(paad_hal_clock_read_ns(&a.hal)) -
			      static_cast<int64_t>(paad_hal_clock_read_ns(&b.hal));
	EXPECT_EQ(drift, 350000); // 35 ppm over 10 s

	// Disciplining b by the measured ratio brings it back in step.
	ASSERT_EQ(paad_hal_clock_adjust_frequency(&b.hal, 35000), PAAD_HAL_OK);
	ASSERT_EQ(paad_hal_clock_step(&b.hal, drift), PAAD_HAL_OK);
	time.now += 10 * kSec;
	EXPECT_EQ(paad_hal_clock_read_ns(&a.hal), paad_hal_clock_read_ns(&b.hal));

	paad_sim_clock_deinit(&a);
	paad_sim_clock_deinit(&b);
}

TEST(SimClock, DefaultSourceIsMonotonic)
{
	paad_sim_clock c{};
	ASSERT_EQ(paad_sim_clock_init(&c, nullptr, nullptr, 0, 0), PAAD_HAL_OK);
	uint64_t t1 = paad_hal_clock_read_ns(&c.hal);
	uint64_t t2 = paad_hal_clock_read_ns(&c.hal);
	EXPECT_LE(t1, t2);
	paad_sim_clock_deinit(&c);
}
