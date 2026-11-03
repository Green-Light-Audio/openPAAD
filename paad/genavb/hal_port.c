/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * GenAVB network port driver on top of the openPAAD HAL.
 *
 * One driver for every platform that implements the HAL: a GenAVB port of
 * type HAL_t uses network port drv_index of the platform, its gPTP clock and
 * the platform timers on that clock. See docs/decisions/0010-hal-design.md.
 *
 * Time: GenAVB counts hardware time in "cycles" of a counter that it turns
 * into nanoseconds. Here the counter is the HAL clock itself, in ns, 64 bits
 * wide, so cycles, hardware time and HAL time are the same number and every
 * timestamp crosses the boundary unchanged. Frequency adjustments go to the
 * HAL clock; GenAVB keeps phase offsets in software.
 *
 * Contexts: HAL event callbacks may run in interrupt context, so they only
 * queue transmit completions; GenAVB's net tx task processes them in
 * tx_cleanup(). Receive is polled by GenAVB's net rx task every period. The
 * link state is polled by a small task, as GenAVB does for PHYs.
 */

#include <stdint.h>
#include <string.h>

#include "config.h"

#include "common/log.h"

#include "hw_clock.h"
#include "hw_timer.h"
#include "net_port.h"
#include "net_rx.h"
#include "ptp.h"

#include "paad/hal.h"
#include "paad/genavb/hal_port.h"

#include "hal_internal.h"

#define NSECS_PER_SEC_U64 1000000000ULL

struct hal_port_timer {
	struct hw_timer hw;
	struct paad_hal_timer *hal;
	bool registered;
};

struct hal_port_tx_done {
	struct net_tx_desc *desc;
	uint32_t flags;
	uint64_t ts;
};

struct hal_port_drv {
	struct net_port *port;
	struct paad_hal_eth *eth;
	struct paad_hal_eth_caps caps;
	unsigned int queue_offset; /* GenAVB queue + offset = HAL queue */

	struct hw_clock clock;
	bool owns_monotonic;
	struct hal_port_timer timers[PAAD_GENAVB_HAL_TIMERS_MAX];

	struct tx_queue_properties tx_q_cap;

	/* Transmit completions: one producer (the HAL event callback), one
	 * consumer (tx_cleanup, under cleanup_lock). in_flight never exceeds
	 * the ring size, so the producer never overruns the consumer. */
	struct hal_port_tx_done done[PAAD_GENAVB_HAL_TX_RING];
	unsigned int done_head;
	unsigned int done_tail;
	unsigned int in_flight;
	rtos_mutex_t cleanup_lock;

	struct net_rx_desc *rx_spare;

	struct paad_hal_eth_link link; /* last state reported to GenAVB */
	bool started;
	struct paad_genavb_hal_stats stats;
};

static struct hal_port_drv hal_drivers[PAAD_GENAVB_HAL_PORTS_MAX];

static struct {
	rtos_thread_t thread;
	bool running;
} link_task;

/* --- Hardware clock ------------------------------------------------------- */

static uint64_t hal_clock_read_counter(void *priv)
{
	return paad_hal_clock_read_ns((struct paad_hal_clock *)priv);
}

static int hal_clock_adj_freq(void *priv, int32_t ppb)
{
	return paad_hal_clock_adjust_frequency((struct paad_hal_clock *)priv, ppb) == PAAD_HAL_OK ? 0 : -1;
}

static void hal_clock_setup(struct hw_clock *clk, struct paad_hal_clock *hal)
{
	memset(clk, 0, sizeof(*clk));
	clk->read_counter = hal_clock_read_counter;
	clk->adj_freq = hal_clock_adj_freq;
	clk->priv = hal;
	/* A 1 GHz, 64-bit counter: one cycle is one nanosecond and it never
	 * wraps in practice. hw_clock handles a period of 2^64 - 1 exactly. */
	clk->rate = (uint32_t)NSECS_PER_SEC_U64;
	clk->period = UINT64_MAX;
	clk->to_ns.shift = 0;
	clk->to_cyc.shift = 0;
}

/* --- Hardware timers ------------------------------------------------------ */

static void hal_timer_fire(void *arg)
{
	struct hal_port_timer *t = arg;

	if (t->hw.func)
		t->hw.func(t->hw.data);
}

static int hal_timer_set_next_event(struct hw_timer *hw, uint64_t cycles)
{
	struct hal_port_timer *t = container_of(hw, struct hal_port_timer, hw);

	return paad_hal_timer_arm(t->hal, cycles, 0) == PAAD_HAL_OK ? 0 : -1;
}

static int hal_timer_cancel_event(struct hw_timer *hw)
{
	struct hal_port_timer *t = container_of(hw, struct hal_port_timer, hw);

	return paad_hal_timer_cancel(t->hal) == PAAD_HAL_OK ? 0 : -1;
}

/* Bind the platform timers that run on this port's clock: the first one to
 * the system monotonic clock if this port provides it, the others to the
 * port clock. hw_timer has no unregister, so a timer slot is registered
 * once and only rebound on later initialisations. */
static void hal_timers_setup(struct hal_port_drv *drv, const struct paad_hal_platform *pf)
{
	unsigned int i, n = 0;

	for (i = 0; i < pf->num_timers && n < PAAD_GENAVB_HAL_TIMERS_MAX; i++) {
		struct paad_hal_timer *hal = pf->timers[i];
		struct hal_port_timer *t;
		hw_clock_id_t id;

		if (hal->clock != drv->eth->clock)
			continue;

		t = &drv->timers[n];
		id = (n == 0 && drv->owns_monotonic) ? HW_CLOCK_MONOTONIC
						    : (hw_clock_id_t)(HW_CLOCK_PORT_BASE + drv->port->hw_clock_id);
		n++;

		t->hal = hal;
		hal->callback = hal_timer_fire;
		hal->callback_arg = t;

		if (t->registered)
			continue;

		t->hw.set_next_event = hal_timer_set_next_event;
		t->hw.cancel_event = hal_timer_cancel_event;
		if (hw_timer_register(id, &t->hw, 0) < 0) {
			os_log(LOG_ERR, "port(%u) hw_timer_register() failed\n", drv->port->index);
			continue;
		}
		t->registered = true;
	}

	if (!n)
		os_log(LOG_ERR, "port(%u) no HAL timer on the port clock\n", drv->port->index);
}

/* --- Transmit ------------------------------------------------------------- */

static void hal_eth_event(struct paad_hal_eth *eth, const struct paad_hal_eth_event *ev, void *arg)
{
	struct hal_port_drv *drv = arg;
	struct hal_port_tx_done *d;
	unsigned int head;

	(void)eth;

	/* Receive is polled every network period and the link by the link
	 * task: only transmit completions need handling here. */
	if (ev->type != PAAD_HAL_ETH_EV_TX_COMPLETE)
		return;

	head = __atomic_load_n(&drv->done_head, __ATOMIC_RELAXED);
	d = &drv->done[head % PAAD_GENAVB_HAL_TX_RING];
	d->desc = (struct net_tx_desc *)ev->cookie;
	d->flags = ev->flags;
	d->ts = ev->timestamp_ns;
	__atomic_store_n(&drv->done_head, head + 1, __ATOMIC_RELEASE);
}

static int hal_send_frame(struct net_port *port, struct net_tx_desc *desc, uint32_t queue)
{
	struct hal_port_drv *drv = net_port_drv(port);
	struct paad_hal_eth_frame frame = {
		.data = (uint8_t *)desc + desc->l2_offset,
		.len = desc->len,
		.flags = (desc->flags & NET_TX_FLAGS_HW_TS) ? PAAD_HAL_ETH_F_TX_TIMESTAMP : 0,
		.cookie = (uintptr_t)desc,
	};

	if (__atomic_add_fetch(&drv->in_flight, 1, __ATOMIC_ACQ_REL) > PAAD_GENAVB_HAL_TX_RING)
		goto busy;

	if (paad_hal_eth_transmit(drv->eth, queue + drv->queue_offset, &frame) != PAAD_HAL_OK)
		goto busy;

	return 0;

busy:
	__atomic_sub_fetch(&drv->in_flight, 1, __ATOMIC_ACQ_REL);
	drv->stats.tx_busy++;

	return -1;
}

static void hal_tx_cleanup(struct net_port *port, uint32_t queue)
{
	struct hal_port_drv *drv = net_port_drv(port);
	unsigned int head, tail;

	(void)queue; /* completions of every queue share one ring */

	rtos_mutex_lock(&drv->cleanup_lock, RTOS_WAIT_FOREVER);

	head = __atomic_load_n(&drv->done_head, __ATOMIC_ACQUIRE);
	for (tail = drv->done_tail; tail != head; tail++) {
		struct hal_port_tx_done *d = &drv->done[tail % PAAD_GENAVB_HAL_TX_RING];
		struct net_tx_desc *desc = d->desc;

		if (desc->flags & NET_TX_FLAGS_HW_TS) {
			if (d->flags & PAAD_HAL_ETH_F_TIMESTAMP_VALID)
				ptp_tx_ts(port, d->ts, desc->priv);
			else
				port->stats[0].tx_ts_err++;
		}

		port_tx_clean_desc(port, desc);
		__atomic_sub_fetch(&drv->in_flight, 1, __ATOMIC_ACQ_REL);
	}
	drv->done_tail = tail;

	rtos_mutex_unlock(&drv->cleanup_lock);
}

/* --- Receive -------------------------------------------------------------- */

static int hal_read_frame_zero_copy(struct net_port *port, struct net_rx_desc **desc, unsigned int *port_index,
				    uint64_t *ts, uint32_t queue)
{
	struct hal_port_drv *drv = net_port_drv(port);
	struct paad_hal_eth_frame frame = { 0 };
	struct net_rx_desc *d;
	int rc;

	(void)queue;

	/* Keep one buffer ready, so that polling an empty port costs no
	 * allocation. */
	if (!drv->rx_spare) {
		drv->rx_spare = net_pool_rx_alloc(NET_PAYLOAD_SIZE_MAX);
		if (!drv->rx_spare) {
			drv->stats.rx_no_buffer++;
			return 0; /* frames stay in the port until buffers are back */
		}
	}

	d = drv->rx_spare;
	frame.data = (uint8_t *)d + d->l2_offset;

	rc = paad_hal_eth_receive(drv->eth, &frame, NET_PAYLOAD_SIZE_MAX);
	if (rc <= 0)
		return rc < 0 ? -1 : 0;

	d->len = frame.len;
	*ts = (frame.flags & PAAD_HAL_ETH_F_TIMESTAMP_VALID) ? frame.timestamp_ns : 0;
	*port_index = port->index;
	*desc = d;
	drv->rx_spare = NULL;

	return 1;
}

/* --- Configuration -------------------------------------------------------- */

static int hal_add_multi(struct net_port *port, uint8_t *addr)
{
	struct hal_port_drv *drv = net_port_drv(port);

	return paad_hal_eth_set_multicast(drv->eth, addr, true) == PAAD_HAL_OK ? 0 : -1;
}

static int hal_del_multi(struct net_port *port, uint8_t *addr)
{
	struct hal_port_drv *drv = net_port_drv(port);

	return paad_hal_eth_set_multicast(drv->eth, addr, false) == PAAD_HAL_OK ? 0 : -1;
}

static int hal_set_tx_queue_config(struct net_port *port, struct tx_queue_properties *cfg)
{
	(void)port;
	(void)cfg;

	return 0; /* the queues and their order are fixed by the HAL */
}

static int hal_set_tx_idle_slope(struct net_port *port, uint64_t idle_slope, uint32_t queue)
{
	struct hal_port_drv *drv = net_port_drv(port);

	if (queue >= drv->tx_q_cap.num_queues || !(drv->tx_q_cap.queue_prop[queue] & TX_QUEUE_FLAGS_CREDIT_SHAPER))
		return -1;

	return paad_hal_eth_set_shaper(drv->eth, queue + drv->queue_offset, idle_slope) == PAAD_HAL_OK ? 0 : -1;
}

static void hal_link_up(struct net_port *port)
{
	(void)port; /* the HAL port runs from init; nothing to configure */
}

static void hal_link_down(struct net_port *port)
{
	(void)port;
}

static const char *const hal_stats_names[] = {
	"tx busy",
	"rx no buffer",
};

static int hal_stats_get_number(struct net_port *port)
{
	(void)port;

	return (int)(sizeof(hal_stats_names) / sizeof(hal_stats_names[0]));
}

static int hal_stats_get_strings(struct net_port *port, const char **buf, unsigned int buf_len)
{
	unsigned int i, n = (unsigned int)hal_stats_get_number(port);

	if (buf_len < n)
		return -1;

	for (i = 0; i < n; i++)
		buf[i] = hal_stats_names[i];

	return 0;
}

static int hal_stats_get(struct net_port *port, uint64_t *buf, unsigned int buf_len)
{
	struct hal_port_drv *drv = net_port_drv(port);

	if (buf_len < (unsigned int)hal_stats_get_number(port))
		return -1;

	buf[0] = drv->stats.tx_busy;
	buf[1] = drv->stats.rx_no_buffer;

	return 0;
}

void hal_port_status(struct net_port *port, struct net_port_status *status)
{
	struct hal_port_drv *drv = net_port_drv(port);

	status->full_duplex = drv->link.full_duplex;
	status->rate = (uint64_t)drv->link.speed_mbps * 1000000ULL;
}

/* --- Link ----------------------------------------------------------------- */

void paad_genavb_hal_poll_link(void)
{
	unsigned int i;

	for (i = 0; i < PAAD_GENAVB_HAL_PORTS_MAX; i++) {
		struct hal_port_drv *drv = &hal_drivers[i];
		struct paad_hal_eth_link link;

		if (!drv->started)
			continue;

		if (paad_hal_eth_get_link(drv->eth, &link) != PAAD_HAL_OK)
			continue;

		if (link.up == drv->link.up &&
		    (!link.up || (link.speed_mbps == drv->link.speed_mbps && link.full_duplex == drv->link.full_duplex)))
			continue;

		/* A speed change while up is a down then an up, as for PHYs. */
		if (drv->link.up) {
			drv->link.up = false;
			port_link_event(drv->port, false, drv->link.speed_mbps, drv->link.full_duplex);
		}

		if (link.up) {
			drv->link = link;
			port_link_event(drv->port, true, link.speed_mbps, link.full_duplex);
		}
	}
}

static void hal_link_task(void *arg)
{
	(void)arg;

	for (;;) {
		paad_genavb_hal_poll_link();
		rtos_sleep(RTOS_MS_TO_TICKS(PAAD_GENAVB_HAL_LINK_POLL_MS));
	}
}

static int hal_post_init(struct net_port *port)
{
	(void)port;

	if (link_task.running)
		return 0;

	if (rtos_thread_create(&link_task.thread, PAAD_GENAVB_HAL_LINK_PRIORITY, 0, PAAD_GENAVB_HAL_LINK_STACK,
			       "hal link", hal_link_task, NULL) < 0) {
		os_log(LOG_ERR, "rtos_thread_create(hal link) failed\n");
		return -1;
	}
	link_task.running = true;

	return 0;
}

static void hal_pre_exit(struct net_port *port)
{
	(void)port;

	if (link_task.running) {
		rtos_thread_abort(&link_task.thread);
		link_task.running = false;
	}
}

/* --- Init and exit -------------------------------------------------------- */

static void hal_exit(struct net_port *port)
{
	struct hal_port_drv *drv = net_port_drv(port);
	unsigned int i;

	paad_genavb_mclk_rec_exit(port);

	if (drv->started) {
		paad_hal_eth_stop(drv->eth);
		drv->started = false;
	}

	/* Completions of frames dropped by stop() release their buffers. */
	hal_tx_cleanup(port, 0);

	for (i = 0; i < PAAD_GENAVB_HAL_TIMERS_MAX; i++)
		if (drv->timers[i].hal)
			paad_hal_timer_cancel(drv->timers[i].hal);

	if (drv->rx_spare) {
		net_rx_free(drv->rx_spare);
		drv->rx_spare = NULL;
	}

	hw_clock_unregister((hw_clock_id_t)(HW_CLOCK_PORT_BASE + port->hw_clock_id));
	if (drv->owns_monotonic) {
		hw_clock_unregister(HW_CLOCK_MONOTONIC);
		drv->owns_monotonic = false;
	}
}

int hal_port_init(struct net_port *port)
{
	const struct paad_hal_platform *pf = paad_hal_platform_get();
	struct hal_port_drv *drv;
	unsigned int i, nq, shaped;

	if (port->drv_index >= PAAD_GENAVB_HAL_PORTS_MAX || !pf || port->drv_index >= pf->num_eth) {
		os_log(LOG_ERR, "port(%u) no HAL network port %u\n", port->index, port->drv_index);
		return -1;
	}

	drv = &hal_drivers[port->drv_index];

	/* Keep the timer slots across initialisations (see hal_timers_setup) */
	{
		struct hal_port_timer timers[PAAD_GENAVB_HAL_TIMERS_MAX];

		memcpy(timers, drv->timers, sizeof(timers));
		memset(drv, 0, sizeof(*drv));
		memcpy(drv->timers, timers, sizeof(timers));
	}

	drv->port = port;
	drv->eth = pf->eth[port->drv_index];
	port->drv = drv;

	if (paad_hal_eth_get_caps(drv->eth, &drv->caps) != PAAD_HAL_OK || !drv->caps.num_tx_queues) {
		os_log(LOG_ERR, "port(%u) HAL get_caps() failed\n", port->index);
		return -1;
	}

	/* GenAVB numbers transmit queues like the HAL: the last is the highest
	 * priority. Use the highest ones if GenAVB supports fewer. */
	nq = drv->caps.num_tx_queues;
	if (nq > MAX_QUEUES)
		nq = MAX_QUEUES;
	if (nq > TX_QUEUE_PROP_MAX)
		nq = TX_QUEUE_PROP_MAX;
	drv->queue_offset = drv->caps.num_tx_queues - nq;
	shaped = drv->caps.num_shaped_queues < nq ? drv->caps.num_shaped_queues : nq;

	drv->tx_q_cap.num_queues = nq;
	for (i = 0; i < nq; i++) {
		drv->tx_q_cap.queue_prop[i] = TX_QUEUE_FLAGS_STRICT_PRIORITY;
		if (i >= nq - shaped)
			drv->tx_q_cap.queue_prop[i] |= TX_QUEUE_FLAGS_CREDIT_SHAPER;
	}

	port->tx_q_cap = &drv->tx_q_cap;
	port->num_tx_q = nq;
	port->num_rx_q = 1;
	/* Timestamps from the HAL are already referenced to the medium. */
	port->rx_tstamp_latency = 0;
	port->tx_tstamp_latency = 0;

	port->drv_ops.add_multi = hal_add_multi;
	port->drv_ops.del_multi = hal_del_multi;
	port->drv_ops.read_frame_zero_copy = hal_read_frame_zero_copy;
	port->drv_ops.send_frame = hal_send_frame;
	port->drv_ops.tx_cleanup = hal_tx_cleanup;
	port->drv_ops.link_up = hal_link_up;
	port->drv_ops.link_down = hal_link_down;
	port->drv_ops.set_tx_queue_config = hal_set_tx_queue_config;
	port->drv_ops.set_tx_idle_slope = hal_set_tx_idle_slope;
	port->drv_ops.stats_get_number = hal_stats_get_number;
	port->drv_ops.stats_get_strings = hal_stats_get_strings;
	port->drv_ops.stats_get = hal_stats_get;
	port->drv_ops.post_init = hal_post_init;
	port->drv_ops.pre_exit = hal_pre_exit;
	port->drv_ops.exit = hal_exit;

	if (rtos_mutex_init(&drv->cleanup_lock) < 0)
		return -1;

	/* Clocks: the port clock, and the system monotonic clock if no other
	 * driver provides one (GenAVB compensates gPTP frequency adjustments
	 * for every clock sharing this hardware clock). */
	hal_clock_setup(&drv->clock, drv->eth->clock);
	if (hw_clock_register((hw_clock_id_t)(HW_CLOCK_PORT_BASE + port->hw_clock_id), &drv->clock) < 0) {
		os_log(LOG_ERR, "port(%u) hw_clock_register() failed\n", port->index);
		return -1;
	}
	if (!hw_clock_get(HW_CLOCK_MONOTONIC)) {
		if (hw_clock_register(HW_CLOCK_MONOTONIC, &drv->clock) < 0)
			goto err_clock;
		drv->owns_monotonic = true;
	}

	hal_timers_setup(drv, pf);

	drv->eth->event_cb = hal_eth_event;
	drv->eth->event_arg = drv;
	if (paad_hal_eth_start(drv->eth, port->mac_addr) != PAAD_HAL_OK) {
		os_log(LOG_ERR, "port(%u) HAL start() failed\n", port->index);
		goto err_monotonic;
	}
	drv->started = true;

	os_log(LOG_INIT, "port(%u) HAL port %u (%s): %u tx queues, %u shaped\n", port->index, port->drv_index,
	       drv->eth->name ? drv->eth->name : "", nq, shaped);

	/* The media clock, if any, is recovered against the gPTP clock of the
	 * first port, like GenAVB's own recovery drivers. Audio still works
	 * without it, from gPTP-derived software clocks. */
	if (port->index == 0 && pf->mclk) {
		if (pf->mclk->ptp_clock != drv->eth->clock)
			os_log(LOG_ERR, "port(%u) media clock captures on another clock: no recovery\n", port->index);
		else if (paad_genavb_mclk_rec_init(port, pf->mclk) < 0)
			os_log(LOG_ERR, "port(%u) media clock recovery not available\n", port->index);
	}

	return 0;

err_monotonic:
	if (drv->owns_monotonic) {
		hw_clock_unregister(HW_CLOCK_MONOTONIC);
		drv->owns_monotonic = false;
	}
err_clock:
	hw_clock_unregister((hw_clock_id_t)(HW_CLOCK_PORT_BASE + port->hw_clock_id));

	return -1;
}

const struct paad_genavb_hal_stats *paad_genavb_hal_stats(unsigned int drv_index)
{
	if (drv_index >= PAAD_GENAVB_HAL_PORTS_MAX)
		return NULL;

	return &hal_drivers[drv_index].stats;
}
