/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* Built without libpcap: no host interfaces. */

#include <stdio.h>

#include "paad/hal/common.h"

#include "host_if.h"

int host_if_open(const char *ifname, struct paad_sim_host_if **out, char *err, size_t err_len)
{
	(void)ifname;
	(void)out;
	snprintf(err, err_len, "built without libpcap: no access to host interfaces");

	return PAAD_HAL_ERR_NOT_SUPPORTED;
}

void host_if_close(struct paad_sim_host_if *h)
{
	(void)h;
}

void host_if_filter(struct paad_sim_host_if *h, const uint8_t addr[6])
{
	(void)h;
	(void)addr;
}

int host_if_send(struct paad_sim_host_if *h, const uint8_t *data, uint16_t len)
{
	(void)h;
	(void)data;
	(void)len;

	return PAAD_HAL_ERR_NOT_SUPPORTED;
}

int host_if_recv(struct paad_sim_host_if *h, uint8_t *buf, uint16_t size, uint64_t *rx_ns)
{
	(void)h;
	(void)buf;
	(void)size;
	(void)rx_ns;

	return 0;
}

bool host_if_up(struct paad_sim_host_if *h)
{
	(void)h;

	return false;
}
