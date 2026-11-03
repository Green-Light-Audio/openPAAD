/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

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
	if (eth->remote)
		return eth->started && eth->peer_up;

	return eth->started && eth->peer && eth->peer->started;
}

/* --- Cable to another process ---------------------------------------------- */

#define CABLE_MAGIC        0x50414144u /* "PAAD" */
#define CABLE_HELLO_NS     100000000ULL
#define CABLE_TIMEOUT_NS   500000000ULL

enum cable_type {
	CABLE_HELLO = 1,
	CABLE_BYE,
	CABLE_FRAME,
	CABLE_NEXT, /* first: our next event time; last: frames received from the peer */
};

/* Same host, same build: native byte order. */
struct cable_hdr {
	uint32_t magic;
	uint16_t type;
	uint16_t len;
	uint64_t first_bit_ns; /* source time the first bit reaches the peer */
	uint64_t last_bit_ns;  /* source time the last bit reaches the peer */
};

static void cable_send(struct paad_sim_eth *eth, enum cable_type type, uint64_t first, uint64_t last,
		       const uint8_t *data, uint16_t len)
{
	uint8_t buf[sizeof(struct cable_hdr) + PAAD_SIM_ETH_MAX_FRAME];
	struct cable_hdr hdr = { CABLE_MAGIC, (uint16_t)type, len, first, last };
	struct sockaddr_un to = { .sun_family = AF_UNIX };

	memcpy(buf, &hdr, sizeof(hdr));
	if (len)
		memcpy(buf + sizeof(hdr), data, len);
	strncpy(to.sun_path, eth->peer_path, sizeof(to.sun_path) - 1);

	/* The peer may not be there (yet): the frame is lost, as on a cable
	 * plugged into nothing. */
	(void)sendto(eth->sock, buf, sizeof(hdr) + len, 0, (const struct sockaddr *)&to, sizeof(to));
}

/* Report a link change if the state differs from the last one reported.
 * Caller holds the lock; returns true if the HAL user must be told. */
static bool link_changed(struct paad_sim_eth *eth)
{
	bool up = link_up(eth);

	if (up == eth->link_reported)
		return false;

	eth->link_reported = up;

	return true;
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

	sim_free(f);

	if (queued)
		notify(dst, &ev);
}

static void complete_tx(void *arg, uint64_t source_ns)
{
	struct tx_done *d = arg;

	(void)source_ns;
	notify(d->eth, &d->ev);
	sim_free(d);
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
	if (eth->remote) {
		eth->last_hello = paad_sim_world_now(eth->world);
		cable_send(eth, CABLE_HELLO, 0, 0, NULL, 0);
		peer_up = link_changed(eth);
	} else {
		peer_up = eth->peer && eth->peer->started;
	}
	sim_unlock(&eth->lock);

	if (peer_up) {
		notify_link(eth);
		if (!eth->remote)
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
	if (eth->remote) {
		cable_send(eth, CABLE_BYE, 0, 0, NULL, 0);
		was_up = link_changed(eth);
	}
	sim_unlock(&eth->lock);

	if (was_up) {
		notify_link(eth);
		if (!eth->remote)
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

	done = sim_calloc(1, sizeof(*done));
	if (!done)
		return PAAD_HAL_ERR_NO_MEMORY;

	sim_lock(&eth->lock);
	if (!eth->started) {
		sim_unlock(&eth->lock);
		sim_free(done);
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

	if (link_up(eth) && eth->remote) {
		cable_send(eth, CABLE_FRAME, start + eth->propagation_ns, end + eth->propagation_ns, frame->data,
			   frame->len);
		eth->arrival[eth->cable_tx % PAAD_SIM_ETH_LOCKSTEP_RING] = end + eth->propagation_ns;
		eth->cable_tx++;
		eth->stats.tx_frames++;
	} else if (link_up(eth)) {
		f = sim_malloc(sizeof(*f) + frame->len);
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
		sim_free(f);
		rc = PAAD_HAL_ERR_NO_MEMORY;
	}
	if (rc == PAAD_HAL_OK && paad_sim_world_schedule(eth->world, end, complete_tx, done) != PAAD_HAL_OK)
		rc = PAAD_HAL_ERR_NO_MEMORY;
	sim_unlock(&eth->lock);

	if (rc != PAAD_HAL_OK)
		sim_free(done);

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
	eth->sock = -1;
	eth->sent_next = UINT64_MAX;

	if (pthread_mutex_init(&eth->lock, NULL) != 0)
		return PAAD_HAL_ERR_IO;

	return PAAD_HAL_OK;
}

void paad_sim_eth_deinit(struct paad_sim_eth *eth)
{
	if (eth->sock >= 0)
		close(eth->sock);
	pthread_mutex_destroy(&eth->lock);
}

/* Read what the peer process sent, keep the link state current. Runs from
 * the world's runner (the simulated interrupt context). */
static void cable_poll(void *arg)
{
	struct paad_sim_eth *eth = arg;
	uint8_t buf[sizeof(struct cable_hdr) + PAAD_SIM_ETH_MAX_FRAME];
	uint64_t now = paad_sim_world_now(eth->world);
	bool notify;

	for (;;) {
		struct cable_hdr hdr;
		struct in_flight *f;
		ssize_t n = recv(eth->sock, buf, sizeof(buf), 0);

		if (n < (ssize_t)sizeof(hdr))
			break; /* nothing more (EAGAIN), or a runt to ignore */

		memcpy(&hdr, buf, sizeof(hdr));
		if (hdr.magic != CABLE_MAGIC || n != (ssize_t)(sizeof(hdr) + hdr.len))
			continue;

		sim_lock(&eth->lock);
		eth->peer_seen = now;
		switch (hdr.type) {
		case CABLE_HELLO:
			if (!eth->peer_up && eth->started) {
				/* answer at once, so the peer sees us without waiting
				 * for our next hello */
				cable_send(eth, CABLE_HELLO, 0, 0, NULL, 0);
				eth->last_hello = now;
			}
			eth->peer_up = true;
			break;
		case CABLE_BYE:
			eth->peer_up = false;
			eth->peer_gone = true;
			break;
		case CABLE_NEXT:
			if (hdr.first_bit_ns > eth->peer_next)
				eth->peer_next = hdr.first_bit_ns;
			if (hdr.last_bit_ns > eth->acked)
				eth->acked = hdr.last_bit_ns;
			break;
		case CABLE_FRAME:
			eth->peer_up = true;
			eth->cable_rx++;
			if (!eth->started || hdr.len > PAAD_SIM_ETH_MAX_FRAME)
				break;
			f = sim_malloc(sizeof(*f) + hdr.len);
			if (!f)
				break;
			f->dst = eth;
			f->first_bit_source_ns = hdr.first_bit_ns;
			f->len = hdr.len;
			memcpy(f->data, buf + sizeof(hdr), hdr.len);
			/* Usually already due: the frame took longer to cross
			 * between processes than to cross the cable. Its
			 * timestamp still comes from first_bit_ns. */
			if (paad_sim_world_schedule(eth->world, hdr.last_bit_ns > now ? hdr.last_bit_ns : now, deliver, f) !=
			    PAAD_HAL_OK)
				sim_free(f);
			break;
		default:
			break;
		}
		notify = link_changed(eth);
		sim_unlock(&eth->lock);

		if (notify)
			notify_link(eth);
	}

	sim_lock(&eth->lock);
	if (eth->started && now - eth->last_hello >= CABLE_HELLO_NS) {
		cable_send(eth, CABLE_HELLO, 0, 0, NULL, 0);
		eth->last_hello = now;
	}
	if (eth->peer_up && now - eth->peer_seen > CABLE_TIMEOUT_NS)
		eth->peer_up = false;
	notify = link_changed(eth);
	sim_unlock(&eth->lock);

	if (notify)
		notify_link(eth);
}

void paad_sim_eth_lockstep_announce(struct paad_sim_eth *eth, uint64_t next_ns)
{
	sim_lock(&eth->lock);
	if (next_ns != eth->sent_next) {
		cable_send(eth, CABLE_NEXT, next_ns, eth->cable_rx, NULL, 0);
		eth->sent_next = next_ns;
	}
	sim_unlock(&eth->lock);
}

uint64_t paad_sim_eth_lockstep_horizon(struct paad_sim_eth *eth)
{
	uint64_t earliest;

	sim_lock(&eth->lock);
	if (eth->peer_gone) {
		earliest = UINT64_MAX;
	} else {
		earliest = eth->peer_next;
		/* The peer has not seen these frames yet: it may answer them
		 * as soon as they arrive. */
		if (eth->cable_tx > eth->acked) {
			uint64_t first = eth->arrival[eth->acked % PAAD_SIM_ETH_LOCKSTEP_RING];

			if (eth->cable_tx - eth->acked >= PAAD_SIM_ETH_LOCKSTEP_RING)
				first = 0; /* too far ahead: wait for the peer */
			if (first < earliest)
				earliest = first;
		}
		earliest = earliest + eth->propagation_ns;
	}
	sim_unlock(&eth->lock);

	return earliest;
}

void paad_sim_eth_lockstep_wait(struct paad_sim_eth *eth, int timeout_ms)
{
	struct pollfd pfd = { .fd = eth->sock, .events = POLLIN };
	int rc;

	if (eth->peer_gone)
		return;

	rc = poll(&pfd, 1, timeout_ms);
	if (rc == 0) {
		sim_lock(&eth->lock);
		eth->peer_gone = true; /* silent for too long: run alone */
		sim_unlock(&eth->lock);
		return;
	}

	cable_poll(eth);
}

void paad_sim_eth_cable_bye(struct paad_sim_eth *eth)
{
	if (!eth->remote)
		return;

	sim_lock(&eth->lock);
	cable_send(eth, CABLE_BYE, 0, 0, NULL, 0);
	sim_unlock(&eth->lock);
}

int paad_sim_eth_connect_socket(struct paad_sim_eth *eth, const char *local_path, const char *peer_path,
				uint32_t propagation_ns)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int size = 1 << 20;
	int fd;

	if (eth->peer || eth->remote ||
	    strlen(local_path) >= sizeof(addr.sun_path) || strlen(peer_path) >= sizeof(eth->peer_path))
		return PAAD_HAL_ERR_INVALID;

	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0)
		return PAAD_HAL_ERR_IO;

	strncpy(addr.sun_path, local_path, sizeof(addr.sun_path) - 1);
	unlink(local_path); /* left over by an earlier run */
	if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0 || fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		close(fd);
		return PAAD_HAL_ERR_IO;
	}

	/* Room for bursts of frames between two polls (the defaults are a few
	 * kilobytes on macOS). Best effort. */
	(void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
	(void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));

	sim_lock(&eth->lock);
	eth->sock = fd;
	strncpy(eth->peer_path, peer_path, sizeof(eth->peer_path) - 1);
	eth->propagation_ns = propagation_ns;
	eth->remote = true;
	sim_unlock(&eth->lock);

	return paad_sim_world_add_poller(eth->world, cable_poll, eth);
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
