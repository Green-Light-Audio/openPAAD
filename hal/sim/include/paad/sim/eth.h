/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * Simulated Ethernet port and point-to-point link (HAL port: sim).
 *
 * Two simulated ports joined by a virtual cable behave like two endpoints
 * plugged into each other: frames take time to serialise at link speed and
 * to propagate, each port timestamps them on its own clock at the moment
 * the first bit crosses the medium, and multicast frames are filtered by
 * the receiver's address list. This is enough for gPTP peer delay, SRP,
 * MAAP, ATDECC and AVTP to run between two simulated endpoints.
 *
 * A port can also be cabled to a port of another process on the same host
 * (paad_sim_eth_connect_socket), so that each process is one device.
 *
 * Limits: one link per port (no simulated bridge yet); transmit queues are
 * served in order of submission, and shaper settings are recorded but not
 * enforced.
 */

#ifndef PAAD_SIM_ETH_H
#define PAAD_SIM_ETH_H

#include <pthread.h>

#include "paad/hal/eth.h"
#include "paad/sim/clock.h"
#include "paad/sim/world.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAAD_SIM_ETH_MAX_FRAME   1518 /* untagged 1514 + VLAN tag, no FCS */
#define PAAD_SIM_ETH_RX_SLOTS    128
#define PAAD_SIM_ETH_MAX_MCAST   32
#define PAAD_SIM_ETH_TX_QUEUES   3
#define PAAD_SIM_ETH_SHAPED      2

struct paad_sim_eth_stats {
	uint64_t tx_frames;
	uint64_t tx_dropped_link_down;
	uint64_t rx_frames;
	uint64_t rx_dropped_filter;
	uint64_t rx_dropped_full;
};

struct paad_sim_eth_rx_slot {
	uint16_t len;
	uint64_t timestamp_ns;
	uint8_t data[PAAD_SIM_ETH_MAX_FRAME];
};

struct paad_sim_eth {
	struct paad_hal_eth hal; /**< the HAL object, first member */
	struct paad_sim_world *world;
	struct paad_sim_clock *clock;

	struct paad_sim_eth *peer;
	uint32_t propagation_ns;
	uint32_t speed_mbps;

	pthread_mutex_t lock;
	bool started;
	uint8_t addr[PAAD_HAL_ETH_ADDR_LEN];
	uint8_t mcast[PAAD_SIM_ETH_MAX_MCAST][PAAD_HAL_ETH_ADDR_LEN];
	unsigned int num_mcast;
	uint64_t tx_busy_until; /**< source time the transmitter is free again */
	uint64_t idle_slope_bps[PAAD_SIM_ETH_TX_QUEUES];

	struct paad_sim_eth_rx_slot rx[PAAD_SIM_ETH_RX_SLOTS];
	unsigned int rx_head;
	unsigned int rx_count;

	struct paad_sim_eth_stats stats;

	/* Cable to a port of another process (paad_sim_eth_connect_socket) */
	bool remote;
	int sock;
	bool peer_up;
	bool link_reported;
	uint64_t peer_seen;  /**< source time of the last message from the peer */
	uint64_t last_hello; /**< source time of the last hello sent */
	char peer_path[104];
};

int paad_sim_eth_init(struct paad_sim_eth *eth, struct paad_sim_world *world, struct paad_sim_clock *clock,
		      const char *name);
void paad_sim_eth_deinit(struct paad_sim_eth *eth);

/** Join two ports with a cable: propagation delay in ns (about 5 ns per
 * metre of copper, plus PHY delays), 1 Gbit/s full duplex. */
int paad_sim_eth_connect(struct paad_sim_eth *a, struct paad_sim_eth *b, uint32_t propagation_ns);

/** Join this port to a port of another process on the same host, through a
 * datagram socket bound at local_path that sends to peer_path. Frames carry
 * their simulated timing: both processes share the host's monotonic clock,
 * so the world must run in real time. The peer is up while it sends
 * (hellos every 100 ms, gone after 500 ms of silence). The world polls the
 * socket (paad_sim_world_add_poller() is done here). */
int paad_sim_eth_connect_socket(struct paad_sim_eth *eth, const char *local_path, const char *peer_path,
				uint32_t propagation_ns);

/** Time a frame of len bytes (no FCS) occupies the wire, in ns, including
 * padding, FCS, preamble and inter-packet gap. */
uint64_t paad_sim_eth_wire_time_ns(uint16_t len, uint32_t speed_mbps);

#ifdef __cplusplus
}
#endif

#endif /* PAAD_SIM_ETH_H */
