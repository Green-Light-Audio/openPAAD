/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// GenAVB's OS clock layer when its time base has not been updated for longer
// than ~4 s (its timekeeping task starved). The software clock conversions
// used to overflow there, sending the monotonic clock 2^32 ns back in time.

#include <cstdint>

#include <gtest/gtest.h>

#include "genavb_clock_fixture.h"

namespace {

constexpr uint64_t kSec = 1000000000ULL;
// GenAVB keeps the rate as a 32-bit binary fraction: a few ns over 10 s.
constexpr double kTolNs = 10;

// Ideal conversions, in floating point.
int64_t scaled(uint64_t ns, double ppb) { return static_cast<int64_t>(static_cast<double>(ns) * (1.0 + ppb / 1e9)); }
int64_t unscaled(uint64_t ns, double ppb) { return static_cast<int64_t>(static_cast<double>(ns) / (1.0 + ppb / 1e9)); }

class GenavbClockTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		gc_set_hw_time(kSec);
		ASSERT_EQ(gc_init(), 0);
		ASSERT_EQ(gc_set_gptp_freq(0), 0);
	}

	uint64_t monotonic()
	{
		uint64_t ns = 0;
		EXPECT_EQ(gc_monotonic(&ns), 0);
		return ns;
	}
};

TEST_F(GenavbClockTest, MonotonicFollowsTheHardwareClockWithoutAdjustment)
{
	uint64_t t0 = monotonic();
	gc_set_hw_time(11 * kSec);
	EXPECT_EQ(monotonic() - t0, 10 * kSec);
}

TEST_F(GenavbClockTest, MonotonicStaysRightLongAfterTheLastTimeBaseUpdate)
{
	// gPTP speeds the shared hardware clock up by 100 ppm; the monotonic
	// clock compensates in software, then nothing touches it for 10 s.
	ASSERT_EQ(gc_set_gptp_freq(100000), 0);
	uint64_t t0 = monotonic();
	gc_set_hw_time(11 * kSec);
	EXPECT_NEAR(static_cast<double>(monotonic() - t0), static_cast<double>(scaled(10 * kSec, -100000)), kTolNs);
}

TEST_F(GenavbClockTest, FrequencyChangeAfterALongGapKeepsTheMonotonicClockContinuous)
{
	ASSERT_EQ(gc_set_gptp_freq(-50000), 0);
	uint64_t t0 = monotonic();
	gc_set_hw_time(21 * kSec);
	uint64_t before = monotonic();
	ASSERT_EQ(gc_set_gptp_freq(70000), 0);
	uint64_t after = monotonic();
	EXPECT_NEAR(static_cast<double>(after), static_cast<double>(before), 1);
	EXPECT_NEAR(static_cast<double>(before - t0), static_cast<double>(scaled(20 * kSec, 50000)), kTolNs);

	gc_set_hw_time(26 * kSec);
	EXPECT_NEAR(static_cast<double>(monotonic() - after), static_cast<double>(scaled(5 * kSec, -70000)), kTolNs);
}

TEST_F(GenavbClockTest, TimesFarAheadConvertToTheHardwareClock)
{
	ASSERT_EQ(gc_set_gptp_freq(100000), 0);
	gc_set_hw_time(6 * kSec);
	uint64_t now = monotonic();

	uint64_t hw = 0;
	ASSERT_EQ(gc_monotonic_to_hw(now + 10 * kSec, &hw), 0);
	EXPECT_NEAR(static_cast<double>(hw - 6 * kSec), static_cast<double>(unscaled(10 * kSec, -100000)), kTolNs);

	uint64_t hw_delta = 0;
	ASSERT_EQ(gc_monotonic_delta_to_hw(10 * kSec, &hw_delta), 0);
	EXPECT_NEAR(static_cast<double>(hw_delta), static_cast<double>(unscaled(10 * kSec, -100000)), kTolNs);
}

} // namespace
