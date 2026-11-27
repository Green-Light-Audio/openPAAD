/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Test runner for code that needs a running FreeRTOS: the tests run inside a
// FreeRTOS task, on the kernel's POSIX port.

#include <cstdio>
#include <unistd.h>

#include <gtest/gtest.h>

#include "FreeRTOS.h"
#include "task.h"

#include "genavb_hal_fixture.h"

namespace {

void runner(void *)
{
	gh_global_init();
	int rc = RUN_ALL_TESTS();
	std::fflush(stdout);
	std::fflush(stderr);
	_exit(rc); // the scheduler never returns to main()
}

} // namespace

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);

	// 1 MiB of stack: GoogleTest is not frugal.
	if (xTaskCreate(runner, "gtest", (1u << 20) / sizeof(StackType_t), nullptr, tskIDLE_PRIORITY + 2, nullptr) != pdPASS)
		return 1;

	vTaskStartScheduler();

	return 1;
}
