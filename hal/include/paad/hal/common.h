/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * openPAAD hardware abstraction layer: common definitions.
 *
 * The HAL is the only boundary between openPAAD's portable core and a
 * hardware platform. Each service is an object made of a pointer to an
 * operations table (provided by the port) and private data. Callers use the
 * inline wrappers, never the tables directly.
 *
 * Conventions for every HAL service:
 *  - Time is expressed in nanoseconds of the gPTP hardware clock
 *    (paad_hal_clock) unless stated otherwise.
 *  - Functions return PAAD_HAL_OK (0) or a negative PAAD_HAL_ERR_* code.
 *  - Functions documented "ISR-safe" may be called from interrupt context
 *    and never block. All other functions are called from task context.
 *  - Callbacks registered with the HAL may run in interrupt context: they
 *    must be short and must not block.
 *  - Optional operations may be NULL; the wrapper then returns
 *    PAAD_HAL_ERR_NOT_SUPPORTED.
 */

#ifndef PAAD_HAL_COMMON_H
#define PAAD_HAL_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** HAL interface version, bumped on any incompatible change. */
#define PAAD_HAL_VERSION_MAJOR 0
#define PAAD_HAL_VERSION_MINOR 1

/** Return codes */
enum paad_hal_status {
	PAAD_HAL_OK = 0,
	PAAD_HAL_ERR_NOT_SUPPORTED = -1, /**< operation not available on this platform */
	PAAD_HAL_ERR_INVALID = -2,       /**< bad argument */
	PAAD_HAL_ERR_BUSY = -3,          /**< resource in use, retry later */
	PAAD_HAL_ERR_NO_MEMORY = -4,     /**< out of buffers or memory */
	PAAD_HAL_ERR_IO = -5,            /**< hardware error */
	PAAD_HAL_ERR_TIMEOUT = -6,       /**< operation did not complete in time */
	PAAD_HAL_ERR_STATE = -7,         /**< object not started, or already started */
};

/** Parts per billion: frequency offsets, positive means faster. */
typedef int32_t paad_hal_ppb_t;

#define PAAD_HAL_NSEC_PER_SEC 1000000000ULL

#ifdef __cplusplus
}
#endif

#endif /* PAAD_HAL_COMMON_H */
