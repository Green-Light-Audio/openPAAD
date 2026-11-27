/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * HAL: platform description.
 *
 * Each port provides exactly one platform: the list of its HAL service
 * instances and its identity sources. The core discovers everything through
 * paad_hal_platform_get(); nothing else is board-specific.
 */

#ifndef PAAD_HAL_PLATFORM_H
#define PAAD_HAL_PLATFORM_H

#include "paad/hal/common.h"
#include "paad/hal/clock.h"
#include "paad/hal/timer.h"
#include "paad/hal/eth.h"
#include "paad/hal/mclk.h"
#include "paad/hal/audio.h"
#include "paad/hal/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

struct paad_hal_platform {
	const char *name;

	/** Board bring-up (clocks, pins, PHYs) before any service is used.
	 * Optional. */
	int (*init)(void);

	struct paad_hal_eth *const *eth;      /**< network ports */
	unsigned int num_eth;

	struct paad_hal_timer *const *timers; /**< timers on the gPTP clock, at least 2 */
	unsigned int num_timers;

	struct paad_hal_mclk *mclk;           /**< NULL on devices without audio */
	struct paad_hal_audio *audio;         /**< NULL on devices without audio I/O */
	struct paad_hal_storage *storage;     /**< NULL: settings are not persisted */

	/** MAC address of network port eth_index (globally unique, or locally
	 * administered for development). Required. */
	int (*get_mac)(unsigned int eth_index, uint8_t mac[PAAD_HAL_ETH_ADDR_LEN]);

	/** Up to len bytes of a value unique to this unit (fuses, serial
	 * number), used to derive the entity ID. Returns the number of bytes
	 * written. Optional. */
	int (*get_unique_id)(uint8_t *buf, size_t len);
};

/** The platform of this build. Implemented once by each port. */
const struct paad_hal_platform *paad_hal_platform_get(void);

#ifdef __cplusplus
}
#endif

#endif /* PAAD_HAL_PLATFORM_H */
