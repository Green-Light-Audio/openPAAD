/*
 * Copyright 2026 AIASK Limited, trading as Green Light Audio
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "hw_clock.h"
#include "hw_timer.h"
#include "net_port.h"
#include "net_rx.h"
#include "ptp.h"

#include "paad/genavb/hal_port.h"
#include "paad/hal.h"
#include "genavb_hal_fixture.h"

static struct net_port test_ports[2];
static const struct paad_hal_platform *platform;
static struct gh_record rec;

/* --- The HAL platform, set by the test ------------------------------------ */

const struct paad_hal_platform *paad_hal_platform_get(void)
{
	return platform;
}

void gh_set_platform(const struct paad_hal_platform *pf)
{
	platform = pf;
}

/* --- GenAVB functions the driver calls, recorded -------------------------- */

struct net_rx_desc *net_pool_rx_alloc(unsigned int size)
{
	struct net_rx_desc *desc;

	if (size > NET_DATA_SIZE - NET_DATA_OFFSET)
		return NULL;

	desc = calloc(1, NET_DATA_SIZE);
	if (desc)
		desc->l2_offset = NET_DATA_OFFSET;

	return desc;
}

void net_rx_free(struct net_rx_desc *desc)
{
	free(desc);
}

void net_tx_free(struct net_tx_desc *desc)
{
	__atomic_add_fetch(&rec.tx_freed, 1, __ATOMIC_RELAXED);
	free(desc);
}

void ptp_tx_ts(struct net_port *phys_port, u64 ts, u32 priv)
{
	rec.ptp_tx_ts_port = phys_port->index;
	rec.ptp_tx_ts_ts = ts;
	rec.ptp_tx_ts_priv = priv;
	__atomic_add_fetch(&rec.ptp_tx_ts_calls, 1, __ATOMIC_RELEASE);
}

void port_link_event(struct net_port *port, bool up, unsigned int speed, unsigned int duplex)
{
	rec.link_port = port->index;
	rec.link_up = up;
	rec.link_speed = speed;
	rec.link_duplex = duplex;
	__atomic_add_fetch(&rec.link_events, 1, __ATOMIC_RELEASE);
}

/* Media clock recovery (mclk_rec.c) needs GenAVB's media clock layer: out
 * of these tests, covered end to end by openpaad_sim_mclk. */
int paad_genavb_mclk_rec_init(struct net_port *port, struct paad_hal_mclk *mclk)
{
	(void)port;
	(void)mclk;

	return -1;
}

void paad_genavb_mclk_rec_exit(struct net_port *port)
{
	(void)port;
}

void gh_reset_record(void)
{
	memset(&rec, 0, sizeof(rec));
}

struct gh_record gh_get_record(void)
{
	struct gh_record r;

	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	r = rec;

	return r;
}

/* GenAVB's log time stamps read the os clock layer, which these tests do not
 * bring up (weak: the full OS layer provides the real one). */
__attribute__((weak)) int os_clock_gettime64(os_clock_id_t clk_id, u64 *ns)
{
	(void)clk_id;
	(void)ns;

	return -1;
}

/* --- Driving the driver --------------------------------------------------- */

void gh_global_init(void)
{
	hw_timer_init();
}

int gh_init(unsigned int i, const uint8_t mac[6])
{
	struct net_port *port = &test_ports[i];

	memset(port, 0, sizeof(*port));
	port->index = i;
	port->drv_type = HAL_t;
	port->drv_index = i;
	port->hw_clock_id = i;
	port->phy_index = (unsigned int)-1;
	memcpy(port->mac_addr, mac, 6);

	return hal_port_init(port);
}

int gh_post_init(unsigned int i)
{
	return test_ports[i].drv_ops.post_init(&test_ports[i]);
}

void gh_exit(unsigned int i)
{
	struct net_port *port = &test_ports[i];

	if (port->drv_ops.pre_exit)
		port->drv_ops.pre_exit(port);
	if (port->drv_ops.exit)
		port->drv_ops.exit(port);
	memset(&port->drv_ops, 0, sizeof(port->drv_ops));
}

unsigned int gh_num_tx_queues(unsigned int i)
{
	return test_ports[i].num_tx_q;
}

unsigned int gh_queue_prop(unsigned int i, unsigned int queue)
{
	return test_ports[i].tx_q_cap->queue_prop[queue];
}

int gh_send(unsigned int i, const uint8_t *data, uint16_t len, bool timestamp, uint32_t priv, unsigned int queue)
{
	struct net_port *port = &test_ports[i];
	struct net_tx_desc *desc = calloc(1, NET_DATA_SIZE);
	int rc;

	if (!desc)
		return -1;

	desc->l2_offset = NET_DATA_OFFSET;
	desc->len = len;
	desc->flags = timestamp ? NET_TX_FLAGS_HW_TS : 0;
	desc->priv = priv;
	memcpy((uint8_t *)desc + desc->l2_offset, data, len);

	rc = port->drv_ops.send_frame(port, desc, queue);
	if (rc < 0)
		free(desc); /* as GenAVB's net tx does */

	return rc;
}

void gh_tx_cleanup(unsigned int i)
{
	test_ports[i].drv_ops.tx_cleanup(&test_ports[i], 0);
}

int gh_receive(unsigned int i, uint8_t *buf, uint16_t *len, uint64_t *ts)
{
	struct net_port *port = &test_ports[i];
	struct net_rx_desc *desc;
	unsigned int port_index = 99;
	int rc;

	rc = port->drv_ops.read_frame_zero_copy(port, &desc, &port_index, ts, 0);
	if (rc != 1)
		return rc;

	if (port_index != i) {
		net_rx_free(desc);
		return -100;
	}

	memcpy(buf, (uint8_t *)desc + desc->l2_offset, desc->len);
	*len = desc->len;
	net_rx_free(desc);

	return 1;
}

int gh_add_multi(unsigned int i, const uint8_t addr[6])
{
	uint8_t a[6];

	memcpy(a, addr, 6);
	return test_ports[i].drv_ops.add_multi(&test_ports[i], a);
}

int gh_del_multi(unsigned int i, const uint8_t addr[6])
{
	uint8_t a[6];

	memcpy(a, addr, 6);
	return test_ports[i].drv_ops.del_multi(&test_ports[i], a);
}

int gh_set_idle_slope(unsigned int i, uint64_t bps, unsigned int queue)
{
	return test_ports[i].drv_ops.set_tx_idle_slope(&test_ports[i], bps, queue);
}

void gh_status(unsigned int i, bool *full_duplex, uint64_t *rate)
{
	struct net_port_status st = { 0 };

	hal_port_status(&test_ports[i], &st);
	*full_duplex = st.full_duplex;
	*rate = st.rate;
}

bool gh_clock_registered(unsigned int id)
{
	return hw_clock_get((hw_clock_id_t)id) != NULL;
}

uint64_t gh_clock_read(unsigned int id)
{
	return hw_clock_read(hw_clock_get((hw_clock_id_t)id));
}

int gh_clock_setfreq(unsigned int id, int32_t ppb)
{
	return hw_clock_setfreq(hw_clock_get((hw_clock_id_t)id), ppb);
}

uint64_t gh_clock_cycles_to_time(unsigned int id, uint64_t cycles)
{
	return hw_clock_cycles_to_time(hw_clock_get((hw_clock_id_t)id), cycles);
}

void *gh_timer_request(unsigned int id, void (*fn)(void *), void *arg)
{
	return hw_timer_request((hw_clock_id_t)id, 0, fn, arg);
}

int gh_timer_arm(void *timer, uint64_t cycles)
{
	return hw_timer_set_next_event((struct hw_timer *)timer, cycles);
}

void gh_timer_free(void *timer)
{
	hw_timer_cancel((struct hw_timer *)timer);
	hw_timer_free((struct hw_timer *)timer);
}
