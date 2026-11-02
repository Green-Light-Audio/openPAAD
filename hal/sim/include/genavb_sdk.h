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

#endif /* OPENPAAD_SIM_GENAVB_SDK_H */
