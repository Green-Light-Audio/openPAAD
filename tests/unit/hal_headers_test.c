/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* The HAL headers compile on their own as strict C11 (-pedantic -Werror). */

#include "paad/hal.h"

int paad_hal_headers_c_version(void);

int paad_hal_headers_c_version(void)
{
	return PAAD_HAL_VERSION_MAJOR * 100 + PAAD_HAL_VERSION_MINOR;
}
