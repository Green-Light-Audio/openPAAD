/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* Between the parts of the GenAVB driver for the HAL (not public). */

#ifndef PAAD_GENAVB_HAL_INTERNAL_H
#define PAAD_GENAVB_HAL_INTERNAL_H

#include "net_port.h"

#include "paad/hal.h"

/* Media clock recovery device on the platform's media clock, following the
 * gPTP clock of port (mclk_rec.c). */
int paad_genavb_mclk_rec_init(struct net_port *port, struct paad_hal_mclk *mclk);
void paad_genavb_mclk_rec_exit(struct net_port *port);

#endif /* PAAD_GENAVB_HAL_INTERNAL_H */
