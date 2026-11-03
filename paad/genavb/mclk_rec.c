/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * GenAVB media clock recovery device on the openPAAD HAL media clock.
 *
 * GenAVB recovers media clocks with a control loop shared by its operating
 * systems (common/os/media_clock_rec_pll_common.c). Each OS layer includes
 * that file and provides a few hooks: start, extend and stop the sampling
 * of the audio clock at gPTP instants, read gPTP time, and adjust the audio
 * PLL. On i.MX, sampling is a 1588 timer compare output wired to a timer
 * capture input. Here it is the HAL's "capture at a gPTP instant", and the
 * PLL is the HAL's frequency adjustment. The loop itself is used unchanged.
 *
 * Captures: GenAVB keeps two sampling instants programmed ahead (start
 * gives two, each capture adds the next). The HAL takes one request at a
 * time, so instants wait in a short queue and the next one is requested as
 * soon as the previous capture has been used. Everything runs in GenAVB's
 * media clock task, in the handler of its 125 us timer, except the capture
 * callback, which may run in interrupt context and only fills a one-slot
 * mailbox.
 *
 * Time: GenAVB names sampling instants in 32-bit gPTP time and the HAL
 * wants a time of its clock. GenAVB's hardware time for a HAL port is that
 * clock in ns (hal_port.c), so GenAVB's own gPTP to hardware time
 * conversion does it.
 */

#include <stdint.h>
#include <string.h>

#include "config.h"

#include "rtos_abstraction_layer.h"

#include "common/log.h"
#include "os/clock.h"

#include "avtp.h"
#include "clock.h"
#include "imx-pll.h"
#include "media_clock.h"
#include "net_port.h"
#include "net_rx.h"
#include "pi.h"
#include "rational.h"

#include "paad/hal.h"
#include "paad/genavb/mclk_rec.h"

#include "hal_internal.h"

#if defined(CONFIG_GENAVB_TSN_AVTP)

/* The shared code's global functions, renamed so that this driver can live
 * in the same image as GenAVB's own recovery drivers. */
#define mclock_rec_pll_common_clean_get       paad_mclk_rec_clean_get
#define mclock_rec_pll_common_config          paad_mclk_rec_config
#define mclock_rec_pll_common_sw_sampling_irq paad_mclk_rec_sw_sampling_irq
#define mclock_rec_pll_common_timer_irq       paad_mclk_rec_timer_irq_common

#include "common/os/media_clock_rec_pll_common.h"

#define REC_QUEUE 4 /* sampling instants waiting, power of 2 */

/* The name the shared code expects. */
struct mclock_rec_pll {
	struct mclock_rec_pll_common c; /* GenAVB's recovery state */
	struct paad_hal_mclk *mclk;
	struct net_port *port; /* whose gPTP clock the captures follow */
	bool registered;
	bool running;

	/* Sampling instants (32-bit gPTP time) not yet requested. */
	uint32_t queue[REC_QUEUE];
	unsigned int q_head;
	unsigned int q_tail;
	bool in_flight; /* requested from the HAL, result not used yet */
	bool stale;     /* that request belongs to a stopped run */

	/* One-slot mailbox filled by the capture callback. */
	uint32_t mailbox_full;
	struct paad_hal_mclk_sample mailbox;

	uint32_t captures;
	uint32_t err_capture;

	/* Stream timestamps written by AVTP, then the write index. */
	unsigned int ts_mem[MCLOCK_REC_NUM_TS + 1];
};

#define mclock_rec_pll_common_to_rec(common) container_of((common), struct mclock_rec_pll, c)
#define mclock_dev_to_rec(m_dev)              container_of((m_dev), struct mclock_rec_pll, c.dev)

static void mclock_rec_pll_reset(struct mclock_rec_pll *rec);

/* The audio PLL is the HAL media clock. */
static int hal_pll_adjust(struct imx_pll *pll, int *ppb);
static unsigned long hal_pll_get_rate(struct imx_pll *pll);
#define imx_pll_adjust   hal_pll_adjust
#define imx_pll_get_rate hal_pll_get_rate

#include "common/os/media_clock_rec_pll_common.c"

extern struct mclock_drv *mclk_drv_h;

static struct mclock_rec_pll hal_rec;

/* --- Audio PLL ------------------------------------------------------------ */

static int hal_pll_adjust(struct imx_pll *pll, int *ppb)
{
	struct mclock_rec_pll *rec = container_of(pll, struct mclock_rec_pll, c.pll);
	paad_hal_ppb_t applied = 0;

	if (paad_hal_mclk_adjust(rec->mclk, *ppb, &applied) != PAAD_HAL_OK)
		return -IMX_CLK_PLL_INVALID_PARAM;

	*ppb = applied;

	return IMX_CLK_PLL_SUCCESS;
}

static unsigned long hal_pll_get_rate(struct imx_pll *pll)
{
	struct mclock_rec_pll *rec = container_of(pll, struct mclock_rec_pll, c.pll);

	return (unsigned long)((uint64_t)rec->mclk->tick_hz * (uint64_t)(1000000000LL + pll->ppb_adjust) /
			       1000000000ULL);
}

/* --- Captures ------------------------------------------------------------- */

/* Any context, one capture at a time. */
static void hal_rec_capture(struct paad_hal_mclk *mclk, const struct paad_hal_mclk_sample *sample, void *arg)
{
	struct mclock_rec_pll *rec = arg;

	(void)mclk;

	rec->mailbox = *sample;
	__atomic_store_n(&rec->mailbox_full, 1, __ATOMIC_RELEASE);
}

static bool rec_take_capture(struct mclock_rec_pll *rec, struct paad_hal_mclk_sample *sample)
{
	if (!__atomic_load_n(&rec->mailbox_full, __ATOMIC_ACQUIRE))
		return false;

	*sample = rec->mailbox;
	__atomic_store_n(&rec->mailbox_full, 0, __ATOMIC_RELAXED);
	rec->in_flight = false;

	if (rec->stale) {
		rec->stale = false;
		return false;
	}

	rec->captures++;

	return true;
}

/* A 32-bit gPTP time, a few tens of ms from now, on the HAL clock. */
static int gptp32_to_hal(struct mclock_rec_pll *rec, uint32_t ts, uint64_t *hal_ns)
{
	os_clock_id_t id = rec->port->clock[PORT_CLOCK_GPTP_0];
	uint64_t now, t;

	if (os_clock_gettime64(id, &now) < 0)
		return -1;

	t = now + (uint64_t)(int64_t)(int32_t)(ts - (uint32_t)now);

	return clock_time_to_cycles(id, t, hal_ns) < 0 ? -1 : 0;
}

static int rec_queue(struct mclock_rec_pll *rec, uint32_t ts)
{
	if (rec->q_head - rec->q_tail >= REC_QUEUE)
		return -1;

	rec->queue[rec->q_head++ & (REC_QUEUE - 1)] = ts;

	return 0;
}

/* Request the next capture if none is pending. An instant the HAL refuses
 * (already passed) is dropped, like a missed hardware event: GenAVB's loop
 * rejects the next measurement and recovers. */
static void rec_request_next(struct mclock_rec_pll *rec)
{
	while (!rec->in_flight && rec->q_tail != rec->q_head) {
		uint32_t ts = rec->queue[rec->q_tail++ & (REC_QUEUE - 1)];
		uint64_t hal_ns;

		if (gptp32_to_hal(rec, ts, &hal_ns) == 0 &&
		    paad_hal_mclk_request_capture(rec->mclk, hal_ns) == PAAD_HAL_OK)
			rec->in_flight = true;
		else
			rec->err_capture++;
	}
}

/* --- Hooks of the shared code --------------------------------------------- */

static void mclock_rec_pll_deferred_adjust(struct mclock_rec_pll *rec)
{
	(void)rec; /* the HAL adjusts from any context */
}

static void mclock_rec_pll_gptp_stop(struct mclock_rec_pll *rec)
{
	rec->q_tail = rec->q_head;
	if (rec->in_flight)
		rec->stale = true;
}

static int mclock_rec_pll_gptp_compare_start(struct mclock_rec_pll *rec, uint32_t ts_0, uint32_t ts_1)
{
	mclock_rec_pll_gptp_stop(rec);

	if (rec_queue(rec, ts_0) < 0 || rec_queue(rec, ts_1) < 0)
		return -1;

	rec_request_next(rec);

	return rec->in_flight ? 0 : -1;
}

static int mclock_rec_pll_gptp_compare_reload(struct mclock_rec_pll *rec, uint32_t next_ts)
{
	return rec_queue(rec, next_ts);
}

static int mclock_rec_pll_gptp_read(struct mclock_rec_pll *rec, uint32_t *now)
{
	return os_clock_gettime32(rec->port->clock[PORT_CLOCK_GPTP_0], now);
}

static unsigned int mclock_shared_mem_read(struct mclock_dev *dev, unsigned int idx)
{
	return mclock_mem_read(dev, idx);
}

/* After an error, the shared code restarts the recovery from the timer
 * handler. */
static void mclock_rec_pll_reset(struct mclock_rec_pll *rec)
{
	mclock_rec_pll_common_stop(&rec->c);
	mclock_rec_pll_common_start(&rec->c, NULL, DEFAULT_REC_PI_KI_FACTOR, DEFAULT_REC_PI_KP_FACTOR);
	rec->c.stats.reset++;
}

/* --- Media clock device ------------------------------------------------------ */

/* GenAVB's media clock task, every 125 us. */
static int hal_rec_timer_irq(struct mclock_dev *dev, void *data, unsigned int ticks)
{
	struct mclock_rec_pll *rec = mclock_dev_to_rec(dev);
	struct paad_hal_mclk_sample sample;

	(void)data;

	if (!rec->running)
		return 0;

	if (rec_take_capture(rec, &sample)) {
		uint32_t cnt = (uint32_t)sample.mclk_ticks;

		mclock_rec_pll_common_timer_irq(&rec->c, MCLOCK_REC_PLL_FLAGS_GPTP_EVENT, cnt - rec->c.audio_pll_cnt_last,
						cnt, ticks);
		rec->c.audio_pll_cnt_last = cnt;
	} else {
		mclock_rec_pll_common_timer_irq(&rec->c, 0, 0, 0, ticks);
	}

	rec_request_next(rec);

	return 0;
}

static int hal_rec_start(struct mclock_dev *dev, struct mclock_start *start)
{
	struct mclock_rec_pll *rec = mclock_dev_to_rec(dev);
	int rc;

	rc = mclock_rec_pll_common_start(&rec->c, start, DEFAULT_REC_PI_KI_FACTOR, DEFAULT_REC_PI_KP_FACTOR);
	if (rc < 0)
		return rc;

	rec->running = true;

	rc = mclock_register_timer(dev, hal_rec_timer_irq, 1);
	if (rc < 0) {
		rec->running = false;
		mclock_rec_pll_common_stop(&rec->c);
		os_log(LOG_ERR, "cannot register timer\n");
	}

	return rc;
}

static int hal_rec_stop(struct mclock_dev *dev)
{
	struct mclock_rec_pll *rec = mclock_dev_to_rec(dev);

	mclock_unregister_timer(dev);
	rec->running = false;
	mclock_rec_pll_common_stop(&rec->c);

	return 0;
}

static int hal_rec_reset(struct mclock_dev *dev)
{
	(void)dev;

	return 0;
}

static int hal_rec_open(struct mclock_dev *dev, int port)
{
	(void)port;

	dev->eth = mclock_dev_to_rec(dev)->port;

	return 0;
}

int paad_genavb_mclk_rec_init(struct net_port *port, struct paad_hal_mclk *mclk)
{
	struct mclock_rec_pll *rec = &hal_rec;
	struct mclock_dev *dev = &rec->c.dev;

	if (rec->registered || !mclk || !mclk->tick_hz || !mclk_drv_h)
		return -1;

	memset(rec, 0, sizeof(*rec));
	rec->mclk = mclk;
	rec->port = port;

	/* Start from the nominal frequency. */
	if (paad_hal_mclk_adjust(mclk, 0, NULL) != PAAD_HAL_OK) {
		os_log(LOG_ERR, "port(%u) media clock adjustment failed\n", port->index);
		return -1;
	}

	rec->c.is_hw_recovery_mode = true;
	rec->c.pll_deferred_adjust = false;
	rec->c.pll_ref_freq = mclk->tick_hz;
	rec->c.pll_timer_clk_div = 1;
	rec->c.pll.parent_rate = mclk->tick_hz;
	rec->c.pll.current_rate = mclk->tick_hz;

	/* Clock domain 0, as GenAVB's own recovery drivers. */
	dev->domain = 0;
	dev->type = REC;
	dev->ts_src = TS_INTERNAL;
	dev->start = hal_rec_start;
	dev->stop = hal_rec_stop;
	dev->reset = hal_rec_reset;
	dev->clean = mclock_rec_pll_common_clean_get;
	dev->open = hal_rec_open;
	dev->release = NULL;
	dev->config = mclock_rec_pll_common_config;

	mclock_rec_pll_common_init(&rec->c);

	dev->sh_mem = rec->ts_mem;
	dev->w_idx = &rec->ts_mem[MCLOCK_REC_NUM_TS];
	dev->sh_mem_size = sizeof(rec->ts_mem);
	dev->num_ts = MCLOCK_REC_NUM_TS;
	dev->timer_period = NET_RX_TX_PERIOD;

	mclk->capture_cb = hal_rec_capture;
	mclk->capture_arg = rec;

	mclock_register_device(dev);
	rec->registered = true;

	os_log(LOG_INIT, "port(%u) media clock recovery on %s (%u Hz)\n", port->index, mclk->name ? mclk->name : "HAL",
	       mclk->tick_hz);

	return 0;
}

void paad_genavb_mclk_rec_exit(struct net_port *port)
{
	struct mclock_rec_pll *rec = &hal_rec;

	if (!rec->registered || rec->port != port)
		return;

	if (rec->running)
		hal_rec_stop(&rec->c.dev);

	mclock_unregister_device(&rec->c.dev);
	rec->mclk->capture_cb = NULL;
	rec->registered = false;
}

static enum paad_genavb_mclk_rec_state rec_state(const struct mclock_rec_pll *rec)
{
	if (!rec->running)
		return PAAD_GENAVB_MCLK_REC_STOPPED;

	switch (rec->c.state) {
	case MEASURE:
		return PAAD_GENAVB_MCLK_REC_MEASURING;
	case ADJUST:
		return PAAD_GENAVB_MCLK_REC_ADJUSTING;
	case ADJUST_LOCKED:
		return PAAD_GENAVB_MCLK_REC_LOCKED;
	default:
		return PAAD_GENAVB_MCLK_REC_STARTING;
	}
}

void paad_genavb_mclk_rec_status(struct paad_genavb_mclk_rec_status *status)
{
	const struct mclock_rec_pll *rec = &hal_rec;

	memset(status, 0, sizeof(*status));

	rtos_mutex_global_lock();

	if (rec->registered) {
		status->registered = true;
		status->from_stream = rec->c.dev.ts_src == TS_EXTERNAL;
		status->state = rec_state(rec);
		status->adjust_ppb = rec->c.pll.ppb_adjust;
		status->phase_err_ticks = rec->c.stats.err_per_sec;
		status->captures = rec->captures;
		status->resets = rec->c.stats.reset;
		status->err_meas = rec->c.stats.err_meas;
		status->err_ts = rec->c.stats.err_ts;
		status->err_wd = rec->c.stats.err_wd;
		status->err_capture = rec->err_capture;
	}

	rtos_mutex_global_unlock();
}

#else /* !CONFIG_GENAVB_TSN_AVTP */

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

void paad_genavb_mclk_rec_status(struct paad_genavb_mclk_rec_status *status)
{
	memset(status, 0, sizeof(*status));
}

#endif /* CONFIG_GENAVB_TSN_AVTP */
