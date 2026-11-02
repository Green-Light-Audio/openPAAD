/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * GenAVB/TSN board description for the host simulator (hal/sim).
 *
 * The simulator presents one virtual Ethernet port to the stack. It reuses
 * the ENET_1G driver type so that GenAVB's port management code needs no
 * change; hal/sim provides the driver functions behind it (Phase 1).
 */

#ifndef OPENPAAD_SIM_GENAVB_SDK_H
#define OPENPAAD_SIM_GENAVB_SDK_H

/* On hardware targets the SDK driver headers pulled in by this file bring
 * these standard headers with them, and parts of GenAVB's OS layer rely on
 * it. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BOARD_NET_RX_PACKETS     8
#define BOARD_NET_RX_PERIOD_MULT 1

#define BOARD_NUM_PORTS      1
#define BOARD_NUM_ENET_PORTS 1

#define BOARD_NET_PORT0_DRV_TYPE          ENET_1G_t
#define BOARD_NET_PORT0_DRV_INDEX         0
#define BOARD_NET_PORT0_DRV_BASE          0
#define BOARD_NET_PORT0_PHY_INDEX         -1
#define BOARD_NET_PORT0_MII_MODE          0
#define BOARD_NET_PORT0_HW_CLOCK          0
#define BOARD_NET_PORT0_TRAFFIC_CLASS_MAX 3
#define BOARD_NET_PORT0_SR_CLASS_MAX      2

/* Board services expected by GenAVB (implemented by the simulator). */
int BOARD_NetPort_Get_MAC(unsigned int port, uint8_t *mac);

#endif /* OPENPAAD_SIM_GENAVB_SDK_H */
