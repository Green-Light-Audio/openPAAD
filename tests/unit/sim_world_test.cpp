/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Simulation world and HAL timers (hal/sim), mostly in virtual time.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "paad/sim/world.h"

namespace {

constexpr uint64_t kUs = 1000ULL;
constexpr uint64_t kMs = 1000000ULL;
constexpr uint64_t kSec = 1000000000ULL;

struct Recorder {
	paad_sim_clock *clock = nullptr;
	std::vector<uint64_t> fired_at; // clock time read inside the callback

	static void cb(void *arg)
	{
		auto *r = static_cast<Recorder *>(arg);
		r->fired_at.push_back(paad_hal_clock_read_ns(&r->clock->hal));
	}
};

class SimWorldTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ASSERT_EQ(paad_sim_world_init(&world, PAAD_SIM_VIRTUAL_TIME), PAAD_HAL_OK);
		// The endpoint's clock starts at an arbitrary value, unrelated to
		// source time, and runs 20 ppm fast.
		ASSERT_EQ(paad_sim_clock_init(&clock, paad_sim_world_now, &world, 7 * kSec, 20000), PAAD_HAL_OK);
		ASSERT_EQ(paad_sim_timer_init(&timer, &world, &clock), PAAD_HAL_OK);
		rec.clock = &clock;
		timer.hal.callback = Recorder::cb;
		timer.hal.callback_arg = &rec;
	}

	void TearDown() override
	{
		paad_sim_timer_deinit(&timer);
		paad_sim_clock_deinit(&clock);
		paad_sim_world_deinit(&world);
	}

	uint64_t now() { return paad_hal_clock_read_ns(&clock.hal); }

	paad_sim_world world{};
	paad_sim_clock clock{};
	paad_sim_timer timer{};
	Recorder rec;
};

} // namespace

TEST_F(SimWorldTest, VirtualTimeOnlyMovesWhenRun)
{
	uint64_t t0 = now();
	EXPECT_EQ(now(), t0);
	ASSERT_EQ(paad_sim_world_run_for(&world, kSec), PAAD_HAL_OK);
	EXPECT_EQ(now(), t0 + kSec + 20000); // 1 s of source time, +20 ppm
}

TEST_F(SimWorldTest, OneShotFiresAtClockTime)
{
	uint64_t expiry = now() + 3 * kMs + 17;
	ASSERT_EQ(paad_hal_timer_arm(&timer.hal, expiry, 0), PAAD_HAL_OK);

	paad_sim_world_run_for(&world, 2 * kMs);
	EXPECT_TRUE(rec.fired_at.empty());

	paad_sim_world_run_for(&world, 2 * kMs);
	ASSERT_EQ(rec.fired_at.size(), 1u);
	EXPECT_GE(rec.fired_at[0], expiry);     // never early
	EXPECT_LE(rec.fired_at[0], expiry + 1); // and at most 1 ns late

	paad_sim_world_run_for(&world, 10 * kMs);
	EXPECT_EQ(rec.fired_at.size(), 1u);
}

TEST_F(SimWorldTest, PeriodicTimerKeepsItsCadence)
{
	uint64_t first = now() + 125 * kUs;
	ASSERT_EQ(paad_hal_timer_arm(&timer.hal, first, 125 * kUs), PAAD_HAL_OK);

	paad_sim_world_run_for(&world, kMs); // 1 ms of source = 1 ms + 20 ns of clock

	ASSERT_EQ(rec.fired_at.size(), 8u);
	for (size_t i = 0; i < rec.fired_at.size(); i++) {
		uint64_t want = first + i * 125 * kUs;
		EXPECT_GE(rec.fired_at[i], want);
		EXPECT_LE(rec.fired_at[i], want + 1);
	}
}

TEST_F(SimWorldTest, CancelStopsTimer)
{
	ASSERT_EQ(paad_hal_timer_arm(&timer.hal, now() + kMs, kMs), PAAD_HAL_OK);
	paad_sim_world_run_for(&world, 2500 * kUs);
	ASSERT_EQ(rec.fired_at.size(), 2u);

	ASSERT_EQ(paad_hal_timer_cancel(&timer.hal), PAAD_HAL_OK);
	paad_sim_world_run_for(&world, 10 * kMs);
	EXPECT_EQ(rec.fired_at.size(), 2u);
}

TEST_F(SimWorldTest, PastExpiryFiresImmediately)
{
	paad_sim_world_run_for(&world, kMs);
	ASSERT_EQ(paad_hal_timer_arm(&timer.hal, now() - 500 * kUs, 0), PAAD_HAL_OK);
	paad_sim_world_run_for(&world, 0);
	EXPECT_EQ(rec.fired_at.size(), 1u);
}

// gPTP servo changes the clock rate while timers are armed: the expiry is
// a clock time, so the timer must follow the new rate.
TEST_F(SimWorldTest, FrequencyChangeAfterArmingIsHonoured)
{
	uint64_t expiry = now() + 10 * kMs;
	ASSERT_EQ(paad_hal_timer_arm(&timer.hal, expiry, 0), PAAD_HAL_OK);
	paad_sim_world_run_for(&world, 5 * kMs);
	ASSERT_EQ(paad_hal_clock_adjust_frequency(&clock.hal, -300000), PAAD_HAL_OK); // -300 ppm

	paad_sim_world_run_for(&world, 20 * kMs);
	ASSERT_EQ(rec.fired_at.size(), 1u);
	EXPECT_GE(rec.fired_at[0], expiry);
	EXPECT_LE(rec.fired_at[0], expiry + 1);
}

TEST_F(SimWorldTest, StepAfterArmingIsHonoured)
{
	uint64_t expiry = now() + 10 * kMs;
	ASSERT_EQ(paad_hal_timer_arm(&timer.hal, expiry, 0), PAAD_HAL_OK);
	ASSERT_EQ(paad_hal_clock_step(&clock.hal, 8 * kMs), PAAD_HAL_OK); // jump forward
	paad_sim_world_run_for(&world, 2 * kMs + 1);
	ASSERT_EQ(rec.fired_at.size(), 1u);
	EXPECT_GE(rec.fired_at[0], expiry);
}

TEST_F(SimWorldTest, EventsRunInTimeThenSchedulingOrder)
{
	std::vector<int> order;
	struct Ctx {
		std::vector<int> *order;
		int id;
	};
	auto fn = [](void *arg, uint64_t) {
		auto *c = static_cast<Ctx *>(arg);
		c->order->push_back(c->id);
	};
	Ctx a{ &order, 1 }, b{ &order, 2 }, c{ &order, 3 }, d{ &order, 4 };
	uint64_t t = paad_sim_world_now(&world);

	ASSERT_EQ(paad_sim_world_schedule(&world, t + 300, fn, &a), PAAD_HAL_OK);
	ASSERT_EQ(paad_sim_world_schedule(&world, t + 100, fn, &b), PAAD_HAL_OK);
	ASSERT_EQ(paad_sim_world_schedule(&world, t + 300, fn, &c), PAAD_HAL_OK);
	ASSERT_EQ(paad_sim_world_schedule(&world, t + 200, fn, &d), PAAD_HAL_OK);

	paad_sim_world_run_for(&world, 250);
	EXPECT_EQ(order, (std::vector<int>{ 2, 4 }));
	paad_sim_world_run_for(&world, 250);
	EXPECT_EQ(order, (std::vector<int>{ 2, 4, 1, 3 }));
}

TEST_F(SimWorldTest, CallbacksMayScheduleAndArm)
{
	// A timer callback re-arms itself as a one-shot: the pattern a core
	// built on a single hardware timer uses.
	struct Ctx {
		paad_sim_timer *timer;
		paad_sim_clock *clock;
		int count = 0;
	} ctx{ &timer, &clock };

	timer.hal.callback = [](void *arg) {
		auto *c = static_cast<Ctx *>(arg);
		if (++c->count < 5)
			paad_hal_timer_arm(&c->timer->hal, paad_hal_clock_read_ns(&c->clock->hal) + 1000, 0);
	};
	timer.hal.callback_arg = &ctx;

	ASSERT_EQ(paad_hal_timer_arm(&timer.hal, now() + 1000, 0), PAAD_HAL_OK);
	paad_sim_world_run_for(&world, kMs);
	EXPECT_EQ(ctx.count, 5);
}

TEST(SimWorldRealTime, TimerFiresInRealTime)
{
	paad_sim_world world{};
	paad_sim_clock clock{};
	paad_sim_timer timer{};
	std::atomic<int> fired{ 0 };

	ASSERT_EQ(paad_sim_world_init(&world, PAAD_SIM_REAL_TIME), PAAD_HAL_OK);
	ASSERT_EQ(paad_sim_clock_init(&clock, paad_sim_world_now, &world, 0, 0), PAAD_HAL_OK);
	ASSERT_EQ(paad_sim_timer_init(&timer, &world, &clock), PAAD_HAL_OK);
	EXPECT_EQ(paad_sim_world_run_for(&world, kMs), PAAD_HAL_ERR_STATE);

	timer.hal.callback = [](void *arg) { static_cast<std::atomic<int> *>(arg)->fetch_add(1); };
	timer.hal.callback_arg = &fired;
	ASSERT_EQ(paad_sim_world_start(&world), PAAD_HAL_OK);

	auto start = std::chrono::steady_clock::now();
	ASSERT_EQ(paad_hal_timer_arm(&timer.hal, paad_hal_clock_read_ns(&clock.hal) + 20 * kMs, 0), PAAD_HAL_OK);
	while (fired.load() == 0 && std::chrono::steady_clock::now() - start < std::chrono::seconds(5))
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	auto elapsed = std::chrono::steady_clock::now() - start;

	EXPECT_EQ(fired.load(), 1);
	EXPECT_GE(elapsed, std::chrono::milliseconds(19)); // not early (1 ms slack for the clocks)

	paad_sim_world_stop(&world);
	paad_sim_timer_deinit(&timer);
	paad_sim_clock_deinit(&clock);
	paad_sim_world_deinit(&world);
}
