/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * FreeRTOS heap storage, placed in OCRAM so that the 256 KiB DTCM stays
 * available for the stack's time-critical data and DMA descriptors.
 * The section is not zeroed at start-up; heap_4 initialises what it needs.
 */

#include <stdint.h>

#include "FreeRTOS.h"

__attribute__((section(".ocram_noinit.freertos_heap"), aligned(32)))
uint8_t ucHeap[configTOTAL_HEAP_SIZE];
