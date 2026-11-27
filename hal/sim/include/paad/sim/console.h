/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * Console of the simulator, safe under FreeRTOS's POSIX port.
 *
 * Use these instead of printf() from FreeRTOS tasks: the C library's locks
 * are not preemption-safe there (see src/console.c).
 */

#ifndef PAAD_SIM_CONSOLE_H
#define PAAD_SIM_CONSOLE_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Send GenAVB's log output to this file (NULL: standard output). */
void paad_sim_console_set_log(FILE *out);

/** Print to standard output. */
void paad_sim_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** Flush standard output and the log file. */
void paad_sim_console_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* PAAD_SIM_CONSOLE_H */
