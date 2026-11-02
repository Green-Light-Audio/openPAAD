/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * GenAVB network port driver on top of the openPAAD HAL.
 *
 * A board selects it per port in its genavb_sdk.h:
 *
 *     #define BOARD_NUM_HAL_PORTS            1
 *     #define BOARD_NET_PORT0_DRV_TYPE       HAL_t
 *     #define BOARD_NET_PORT0_DRV_INDEX      0   (index in paad_hal_platform.eth)
 *     #define BOARD_NET_PORT0_PHY_INDEX      -1  (the HAL handles the PHY)
 *
 * GenAVB then calls hal_port_init() (declared in GenAVB's net_port.h) for
 * that port. The driver needs, on the port's gPTP clock, one HAL timer for
 * GenAVB's network period (on the first HAL port) and one per AVTP clock
 * source and CRF stream.
 */

#ifndef PAAD_GENAVB_HAL_PORT_H
#define PAAD_GENAVB_HAL_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAAD_GENAVB_HAL_PORTS_MAX       2   /* GenAVB endpoint limit */
#define PAAD_GENAVB_HAL_TIMERS_MAX      8   /* per port */
#define PAAD_GENAVB_HAL_TX_RING         64  /* frames in flight per port */
#define PAAD_GENAVB_HAL_LINK_POLL_MS    100
#define PAAD_GENAVB_HAL_LINK_PRIORITY   1
#define PAAD_GENAVB_HAL_LINK_STACK      (RTOS_MINIMAL_STACK_SIZE + 128)

struct paad_genavb_hal_stats {
	uint64_t tx_busy;      /**< frames refused: ring or HAL queue full */
	uint64_t rx_no_buffer; /**< receive polls without a free buffer */
};

/** Check every started HAL port's link and report changes to GenAVB. Runs
 * every PAAD_GENAVB_HAL_LINK_POLL_MS in the driver's link task; exposed for
 * tests. Task context. */
void paad_genavb_hal_poll_link(void);

const struct paad_genavb_hal_stats *paad_genavb_hal_stats(unsigned int drv_index);

#ifdef __cplusplus
}
#endif

#endif /* PAAD_GENAVB_HAL_PORT_H */
