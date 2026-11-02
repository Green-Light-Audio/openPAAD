/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Board bring-up entry points shared by every openPAAD application.
 */

#ifndef OPENPAAD_BOARD_H
#define OPENPAAD_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Full early initialisation: MPU, pins, core and peripheral clocks, debug
 * console, Ethernet glue. Call once from main() before the scheduler starts. */
void openpaad_board_init(void);

/* Individual steps, exposed for applications with special needs. */
void openpaad_board_clocks_init(void);
void openpaad_board_enet_init(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENPAAD_BOARD_H */
