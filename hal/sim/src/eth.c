/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdlib.h>
#include <string.h>

#include "paad/sim/eth.h"

#include "lock.h"

#define ETH_MIN_LEN   60 /* without FCS */
#define ETH_FCS       4
#define ETH_PREAMBLE  8
#define ETH_IPG       12

static const uint8_t broadcast_addr[PAAD_HAL_ETH_ADDR_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

struct in_flight {
	struct paad_sim_eth *dst;
	uint64_t first_bit_source_ns; /* arrival of the first bit at dst */
	uint16_t len;
	uint8_t data[];
};

struct tx_done {
	struct paad_sim_eth *eth;
	struct paad_hal_eth_event ev;
};

static inline struct paad_sim_eth *to_sim(struct paad_hal_eth *hal)
{
	return (struct paad_sim_eth *)hal;
}

static void notify(struct paad_sim_eth *eth, const struct paad_hal_eth_event *ev)
{
	if (eth->hal.event_cb)
		eth->hal.event_cb(&eth->hal, ev, eth->hal.event_arg);
}

static void notify_link(struct paad_sim_eth *eth)
{
	struct paad_hal_eth_event ev = { .type = PAAD_HAL_ETH_EV_LINK };

	notify(eth, &ev);
}

uint64_t paad_sim_eth_wire_time_ns(uint16_t len, uint32_t speed_mbps)
{
	uint64_t bytes = (len < ETH_MIN_LEN ? ETH_MIN_LEN : len) + ETH_FCS + ETH_PREAMBLE + ETH_IPG;

	return bytes * 8 * 1000 / speed_mbps;
}

static bool link_up(const struct paad_sim_eth *eth)
{
	return eth->started && eth->peer && eth->peer->started;
}

/* Receiver side: called by the world when a frame has fully arrived. */
static bool accepts(struct paad_sim_eth *eth, const uint8_t *dst)
{
	unsigned int i;

	if (!memcmp(dst, eth->addr, PAAD_HAL_ETH_ADDR_LEN) || !memcmp(dst, broadcast_addr, PAAD_HAL_ETH_ADDR_LEN))
		return true;

	if (!(dst[0] & 1))
		return false;

	for (i = 0; i < eth->num_mcast; i++)
		if (!memcmp(dst, eth->mcast[i], PAAD_HAL_ETH_ADDR_LEN))
			return true;

	return false;
}

static void deliver(void *arg, uint64_t source_ns)
{
	struct in_flight *f = arg;
	struct paad_sim_eth *dst = f->dst;
	struct paad_hal_eth_event ev = { .type = PAAD_HAL_ETH_EV_RX_READY };
	bool queued = false;

	(void)source_ns;

	sim_lock(&dst->lock);
	if (!dst->started) {
		/* port went down while the frame was on the wire */
	} else if (f->len < PAAD_HAL_ETH_ADDR_LEN || !accepts(dst, f->data)) {
		dst->stats.rx_dropped_filter++;
	} else if (dst->rx_count == PAAD_SIM_ETH_RX_SLOTS) {
		dst->stats.rx_dropped_full++;
	} else {
		struct paad_sim_eth_rx_slot *slot = &dst->rx[(dst->rx_head + dst->rx_count) % PAAD_SIM_ETH_RX_SLOTS];

		slot->len = f->len;
		slot->timestamp_ns = paad_sim_clock_at(dst->clock, f->first_bit_source_ns);
		memcpy(slot->data, f->data, f->len);
		dst->rx_count++;
		dst->stats.rx_frames++;
		queued = true;
	}
	sim_unlock(&dst->lock);

	free(f);

	if (queued)
		notify(dst, &ev);
}

static void complete_tx(void *arg, uint64_t source_ns)
{
	struct tx_done *d = arg;

	(void)source_ns;
	notify(d->eth, &d->ev);
	free(d);
}

/* --- HAL operations ------------------------------------------------------- */

static int sim_start(struct paad_hal_eth *hal, const uint8_t addr[PAAD_HAL_ETH_ADDR_LEN])
{
	struct paad_sim_eth *eth = to_sim(hal);
	bool peer_up;

	sim_lock(&eth->lock);
	if (eth->started) {
		sim_unlock(&eth->lock);
		return PAAD_HAL_ERR_STATE;
	}
	memcpy(eth->addr, addr, PAAD_HAL_ETH_ADDR_LEN);
	eth->started = true;
	peer_up = eth->peer && eth->peer->started;
	sim_unlock(&eth->lock);

	if (peer_up) {
		notify_link(eth);
		notify_link(eth->peer);
	}

	return PAAD_HAL_OK;
}

static void sim_stop(struct paad_hal_eth *hal)
{
	struct paad_sim_eth *eth = to_sim(hal);
	bool was_up;

	sim_lock(&eth->lock);
	was_up = link_up(eth);
	eth->started = false;
	eth->rx_head = 0;
	eth->rx_count = 0;
	sim_unlock(&eth->lock);

	if (was_up) {
		notify_link(eth);
		notify_link(eth->peer);
	}
}

static int sim_get_caps(struct paad_hal_eth *hal, struct paad_hal_eth_caps *caps)
{
	(void)hal;

	caps->num_tx_queues = PAAD_SIM_ETH_TX_QUEUES;
	caps->num_shaped_queues = PAAD_SIM_ETH_SHAPED;
	caps->max_frame_len = PAAD_SIM_ETH_MAX_FRAME;
	caps->tx_timestamps = true;
	caps->rx_timestamps = true;

	return PAAD_HAL_OK;
}

static int sim_get_link(struct paad_hal_eth *hal, struct paad_hal_eth_link *link)
{
	struct paad_sim_eth *eth = to_sim(hal);

	link->up = link_up(eth);
	link->speed_mbps = link->up ? eth->speed_mbps : 0;
	link->full_duplex = link->up;

	return PAAD_HAL_OK;
}

static int sim_set_multicast(struct paad_hal_eth *hal, const uint8_t addr[PAAD_HAL_ETH_ADDR_LEN], bool add)
{
	struct paad_sim_eth *eth = to_sim(hal);
	int rc = PAAD_HAL_OK;
	unsigned int i;

	if (!(addr[0] & 1))
		return PAAD_HAL_ERR_INVALID;

	sim_lock(&eth->lock);
	for (i = 0; i < eth->num_mcast; i++)
		if (!memcmp(eth->mcast[i], addr, PAAD_HAL_ETH_ADDR_LEN))
			break;

	if (add && i == eth->num_mcast) {
		if (eth->num_mcast < PAAD_SIM_ETH_MAX_MCAST)
			memcpy(eth->mcast[eth->num_mcast++], addr, PAAD_HAL_ETH_ADDR_LEN);
		else
			rc = PAAD_HAL_ERR_NO_MEMORY;
	} else if (!add && i < eth->num_mcast) {
		memmove(eth->mcast[i], eth->mcast[i + 1], (eth->num_mcast - i - 1) * PAAD_HAL_ETH_ADDR_LEN);
		eth->num_mcast--;
	}
	sim_unlock(&eth->lock);

	return rc;
}

static int sim_transmit(struct paad_hal_eth *hal, unsigned int queue, const struct paad_hal_eth_frame *frame)
{
	struct paad_sim_eth *eth = to_sim(hal);
	struct tx_done *done;
	struct in_flight *f = NULL;
	uint64_t now, start, end;
	int rc = PAAD_HAL_OK;

	if (queue >= PAAD_SIM_ETH_TX_QUEUES || !frame->data || frame->len < 14 || frame->len > PAAD_SIM_ETH_MAX_FRAME)
		return PAAD_HAL_ERR_INVALID;

	done = calloc(1, sizeof(*done));
	if (!done)
		return PAAD_HAL_ERR_NO_MEMORY;

	sim_lock(&eth->lock);
	if (!eth->started) {
		sim_unlock(&eth->lock);
		free(done);
		return PAAD_HAL_ERR_STATE;
	}

	now = paad_sim_world_now(eth->world);
	start = now > eth->tx_busy_until ? now : eth->tx_busy_until;
	end = start + paad_sim_eth_wire_time_ns(frame->len, eth->speed_mbps);
	eth->tx_busy_until = end;

	done->eth = eth;
	done->ev.type = PAAD_HAL_ETH_EV_TX_COMPLETE;
	done->ev.cookie = frame->cookie;
	if (frame->flags & PAAD_HAL_ETH_F_TX_TIMESTAMP) {
		done->ev.flags = PAAD_HAL_ETH_F_TIMESTAMP_VALID;
		done->ev.timestamp_ns = paad_sim_clock_at(eth->clock, start);
	}

	if (link_up(eth)) {
		f = malloc(sizeof(*f) + frame->len);
		if (!f) {
			rc = PAAD_HAL_ERR_NO_MEMORY;
		} else {
			f->dst = eth->peer;
			f->first_bit_source_ns = start + eth->propagation_ns;
			f->len = frame->len;
			memcpy(f->data, frame->data, frame->len);
		}
		eth->stats.tx_frames++;
	} else {
		eth->stats.tx_dropped_link_down++;
	}

	/* Delivery once the last bit has reached the peer, completion once it
	 * has left this port. */
	if (f && paad_sim_world_schedule(eth->world, end + eth->propagation_ns, deliver, f) != PAAD_HAL_OK) {
		free(f);
		rc = PAAD_HAL_ERR_NO_MEMORY;
	}
	if (rc == PAAD_HAL_OK && paad_sim_world_schedule(eth->world, end, complete_tx, done) != PAAD_HAL_OK)
		rc = PAAD_HAL_ERR_NO_MEMORY;
	sim_unlock(&eth->lock);

	if (rc != PAAD_HAL_OK)
		free(done);

	return rc;
}

static int sim_receive(struct paad_hal_eth *hal, struct paad_hal_eth_frame *frame, uint16_t buf_len)
{
	struct paad_sim_eth *eth = to_sim(hal);
	struct paad_sim_eth_rx_slot *slot;
	int rc = 0;

	sim_lock(&eth->lock);
	if (eth->rx_count) {
		slot = &eth->rx[eth->rx_head];
		if (slot->len > buf_len) {
			rc = PAAD_HAL_ERR_INVALID;
		} else {
			memcpy(frame->data, slot->data, slot->len);
			frame->len = slot->len;
			frame->flags = PAAD_HAL_ETH_F_TIMESTAMP_VALID;
			frame->timestamp_ns = slot->timestamp_ns;
			eth->rx_head = (eth->rx_head + 1) % PAAD_SIM_ETH_RX_SLOTS;
			eth->rx_count--;
			rc = 1;
		}
	}
	sim_unlock(&eth->lock);

	return rc;
}

static int sim_set_shaper(struct paad_hal_eth *hal, unsigned int queue, uint64_t idle_slope_bps)
{
	struct paad_sim_eth *eth = to_sim(hal);

	if (queue >= PAAD_SIM_ETH_TX_QUEUES || queue < PAAD_SIM_ETH_TX_QUEUES - PAAD_SIM_ETH_SHAPED)
		return PAAD_HAL_ERR_INVALID;

	sim_lock(&eth->lock);
	eth->idle_slope_bps[queue] = idle_slope_bps;
	sim_unlock(&eth->lock);

	return PAAD_HAL_OK;
}

static const struct paad_hal_eth_ops sim_eth_ops = {
	.start = sim_start,
	.stop = sim_stop,
	.get_caps = sim_get_caps,
	.get_link = sim_get_link,
	.set_multicast = sim_set_multicast,
	.transmit = sim_transmit,
	.receive = sim_receive,
	.set_shaper = sim_set_shaper,
};

int paad_sim_eth_init(struct paad_sim_eth *eth, struct paad_sim_world *world, struct paad_sim_clock *clock,
		      const char *name)
{
	memset(eth, 0, sizeof(*eth));
	eth->hal.ops = &sim_eth_ops;
	eth->hal.name = name ? name : "sim-eth";
	eth->hal.clock = &clock->hal;
	eth->world = world;
	eth->clock = clock;
	eth->speed_mbps = 1000;

	if (pthread_mutex_init(&eth->lock, NULL) != 0)
		return PAAD_HAL_ERR_IO;

	return PAAD_HAL_OK;
}

void paad_sim_eth_deinit(struct paad_sim_eth *eth)
{
	pthread_mutex_destroy(&eth->lock);
}

int paad_sim_eth_connect(struct paad_sim_eth *a, struct paad_sim_eth *b, uint32_t propagation_ns)
{
	if (a == b || a->peer || b->peer)
		return PAAD_HAL_ERR_INVALID;

	a->peer = b;
	b->peer = a;
	a->propagation_ns = propagation_ns;
	b->propagation_ns = propagation_ns;

	return PAAD_HAL_OK;
}
