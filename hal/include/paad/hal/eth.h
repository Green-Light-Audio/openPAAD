/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * HAL service: Ethernet port with hardware timestamps and traffic shaping.
 *
 * What makes an AVB endpoint work lives here: frames timestamped in hardware
 * on both directions, several transmit queues, and credit-based shaping
 * (IEEE 802.1Qav) on the stream queues so that class A frames leave every
 * 125 us without software jitter.
 *
 * Timestamps are on the port's gPTP clock (paad_hal_eth.clock) and already
 * corrected for PHY and MAC latencies at the current link speed: they refer
 * to the moment the frame's first bit crosses the network medium.
 */

#ifndef PAAD_HAL_ETH_H
#define PAAD_HAL_ETH_H

#include "paad/hal/common.h"
#include "paad/hal/clock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAAD_HAL_ETH_ADDR_LEN 6

/** Frame flags */
#define PAAD_HAL_ETH_F_TX_TIMESTAMP     (1u << 0) /**< tx: report the transmit timestamp */
#define PAAD_HAL_ETH_F_TIMESTAMP_VALID  (1u << 1) /**< rx or tx completion: timestamp_ns is valid */

/** One frame, from the destination MAC address to the end of the payload
 * (no preamble, no FCS). */
struct paad_hal_eth_frame {
	uint8_t *data;
	uint16_t len;
	uint32_t flags;
	uint64_t timestamp_ns; /**< receive time, or transmit time on completion */
	uintptr_t cookie;      /**< tx: opaque value returned in the completion event */
};

struct paad_hal_eth_link {
	bool up;
	uint32_t speed_mbps; /**< 10, 100, 1000 ... */
	bool full_duplex;
};

struct paad_hal_eth_caps {
	/** Transmit queues, strict priority: queue (num_tx_queues - 1) has the
	 * highest priority. */
	uint8_t num_tx_queues;
	/** Queues with a credit-based shaper: the highest-priority ones. Stream
	 * classes need at least 1 (class A) and preferably 2 (classes A and B). */
	uint8_t num_shaped_queues;
	uint16_t max_frame_len;   /**< largest frame accepted by transmit, bytes */
	bool tx_timestamps;       /**< hardware transmit timestamps available */
	bool rx_timestamps;       /**< hardware receive timestamps available */
};

/** Events reported to the core */
enum paad_hal_eth_event_type {
	PAAD_HAL_ETH_EV_RX_READY,    /**< at least one frame is waiting in receive() */
	PAAD_HAL_ETH_EV_TX_COMPLETE, /**< a frame left the port; its buffer may be reused */
	PAAD_HAL_ETH_EV_LINK,        /**< link state changed; read it with get_link() */
};

struct paad_hal_eth_event {
	enum paad_hal_eth_event_type type;
	uintptr_t cookie;      /**< TX_COMPLETE: the frame's cookie */
	uint32_t flags;        /**< TX_COMPLETE: PAAD_HAL_ETH_F_TIMESTAMP_VALID if timestamped */
	uint64_t timestamp_ns; /**< TX_COMPLETE: transmit time */
};

struct paad_hal_eth;

/** May run in interrupt context. */
typedef void (*paad_hal_eth_event_cb)(struct paad_hal_eth *eth, const struct paad_hal_eth_event *ev, void *arg);

struct paad_hal_eth_ops {
	/** Bring the port up with this unicast MAC address. Required. */
	int (*start)(struct paad_hal_eth *eth, const uint8_t addr[PAAD_HAL_ETH_ADDR_LEN]);

	/** Bring the port down; pending transmissions complete or are
	 * dropped, each with a TX_COMPLETE event. Required. */
	void (*stop)(struct paad_hal_eth *eth);

	/** Required. */
	int (*get_caps)(struct paad_hal_eth *eth, struct paad_hal_eth_caps *caps);

	/** ISR-safe. Required. */
	int (*get_link)(struct paad_hal_eth *eth, struct paad_hal_eth_link *link);

	/** Accept (add) or stop accepting (remove) a multicast address.
	 * Required: gPTP, MSRP, ATDECC and every stream use multicast. */
	int (*set_multicast)(struct paad_hal_eth *eth, const uint8_t addr[PAAD_HAL_ETH_ADDR_LEN], bool add);

	/** Queue a frame for transmission on a transmit queue. The data must
	 * stay valid until the frame's TX_COMPLETE event. Returns
	 * PAAD_HAL_ERR_BUSY when the queue is full. Required. */
	int (*transmit)(struct paad_hal_eth *eth, unsigned int queue, const struct paad_hal_eth_frame *frame);

	/** Copy the next received frame into frame->data (capacity buf_len)
	 * and set len, flags and timestamp_ns. Returns 1 if a frame was read,
	 * 0 if none was waiting, or a negative error. Non-blocking. Required. */
	int (*receive)(struct paad_hal_eth *eth, struct paad_hal_eth_frame *frame, uint16_t buf_len);

	/** Configure the credit-based shaper of a shaped queue with the
	 * bandwidth reserved for it, in bit/s (the 802.1Qav idle slope). 0
	 * turns shaping off. Required on ports that carry streams. */
	int (*set_shaper)(struct paad_hal_eth *eth, unsigned int queue, uint64_t idle_slope_bps);
};

struct paad_hal_eth {
	const struct paad_hal_eth_ops *ops;
	void *priv;
	const char *name;
	struct paad_hal_clock *clock; /**< clock of every timestamp of this port */
	paad_hal_eth_event_cb event_cb; /**< set by the core before start() */
	void *event_arg;
};

static inline int paad_hal_eth_start(struct paad_hal_eth *eth, const uint8_t addr[PAAD_HAL_ETH_ADDR_LEN])
{
	return eth->ops->start(eth, addr);
}

static inline void paad_hal_eth_stop(struct paad_hal_eth *eth)
{
	eth->ops->stop(eth);
}

static inline int paad_hal_eth_get_caps(struct paad_hal_eth *eth, struct paad_hal_eth_caps *caps)
{
	return eth->ops->get_caps(eth, caps);
}

static inline int paad_hal_eth_get_link(struct paad_hal_eth *eth, struct paad_hal_eth_link *link)
{
	return eth->ops->get_link(eth, link);
}

static inline int paad_hal_eth_set_multicast(struct paad_hal_eth *eth, const uint8_t addr[PAAD_HAL_ETH_ADDR_LEN], bool add)
{
	return eth->ops->set_multicast(eth, addr, add);
}

static inline int paad_hal_eth_transmit(struct paad_hal_eth *eth, unsigned int queue, const struct paad_hal_eth_frame *frame)
{
	return eth->ops->transmit(eth, queue, frame);
}

static inline int paad_hal_eth_receive(struct paad_hal_eth *eth, struct paad_hal_eth_frame *frame, uint16_t buf_len)
{
	return eth->ops->receive(eth, frame, buf_len);
}

static inline int paad_hal_eth_set_shaper(struct paad_hal_eth *eth, unsigned int queue, uint64_t idle_slope_bps)
{
	if (!eth->ops->set_shaper)
		return PAAD_HAL_ERR_NOT_SUPPORTED;

	return eth->ops->set_shaper(eth, queue, idle_slope_bps);
}

#ifdef __cplusplus
}
#endif

#endif /* PAAD_HAL_ETH_H */
