/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* A network interface of the host, for raw Ethernet frames (not public:
 * paad_sim_eth_connect_host() in paad/sim/eth.h uses it). */

#ifndef PAAD_SIM_HOST_IF_H
#define PAAD_SIM_HOST_IF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct paad_sim_host_if;

/* Open ifname for raw frames, in promiscuous mode. Returns a PAAD_HAL_*
 * code; on error, err says why in words. */
int host_if_open(const char *ifname, struct paad_sim_host_if **out, char *err, size_t err_len);
void host_if_close(struct paad_sim_host_if *h);

/* Only pass frames sent to addr or to a group address, and not sent by
 * addr itself (the host shows us our own frames too). Best effort: the
 * caller filters again. */
void host_if_filter(struct paad_sim_host_if *h, const uint8_t addr[6]);

int host_if_send(struct paad_sim_host_if *h, const uint8_t *data, uint16_t len);

/* Next received frame, if any: its length (0: none, < 0: error), and in
 * *rx_ns the time the host received it, on paad_sim_monotonic_ns()'s clock
 * (0 if the host gave no time). */
int host_if_recv(struct paad_sim_host_if *h, uint8_t *buf, uint16_t size, uint64_t *rx_ns);

/* The interface is up and has a carrier. */
bool host_if_up(struct paad_sim_host_if *h);

#endif
