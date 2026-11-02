/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* The debug console of the RTOS abstraction layer: standard output on the
 * host (the MCUXpresso SDK's debug console on hardware). */

#include <stdarg.h>
#include <stdio.h>

int DbgConsole_Vprintf(const char *fmt, va_list ap);

int DbgConsole_Vprintf(const char *fmt, va_list ap)
{
	return vprintf(fmt, ap);
}
