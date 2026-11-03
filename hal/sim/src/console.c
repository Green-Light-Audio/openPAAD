/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* The debug console of the RTOS abstraction layer (GenAVB's log output) on
 * the host, and openpaad-sim's own messages.
 *
 * FreeRTOS's POSIX port switches tasks with signals, so a task can be
 * suspended inside a C library call that holds a lock (stdio, malloc); the
 * next task needing that lock then waits forever. Every write here runs
 * with preemption disabled. */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "paad/sim/console.h"

static FILE *log_out; /* NULL: standard output */

static bool scheduler_running(void)
{
	return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

static void locked_vprint(FILE *out, const char *fmt, va_list ap)
{
	if (scheduler_running()) {
		taskENTER_CRITICAL();
		vfprintf(out, fmt, ap);
		taskEXIT_CRITICAL();
	} else {
		vfprintf(out, fmt, ap);
	}
}

void paad_sim_console_set_log(FILE *out)
{
	log_out = out;
}

void paad_sim_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	locked_vprint(stdout, fmt, ap);
	va_end(ap);
}

void paad_sim_console_flush(void)
{
	if (scheduler_running()) {
		taskENTER_CRITICAL();
		fflush(NULL);
		taskEXIT_CRITICAL();
	} else {
		fflush(NULL);
	}
}

int DbgConsole_Vprintf(const char *fmt, va_list ap);

int DbgConsole_Vprintf(const char *fmt, va_list ap)
{
	locked_vprint(log_out ? log_out : stdout, fmt, ap);

	return 0;
}
