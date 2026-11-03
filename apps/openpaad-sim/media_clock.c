/*
 * Copyright 2026 openPAAD contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * openpaad-sim media clock report.
 *
 * GenAVB recovers the device's media clock (paad/genavb/mclk_rec.c): a
 * talker's to gPTP, a listener's to the stream it receives. Both should end
 * up running at gPTP rate, since the talker's stream is timed by gPTP. The
 * simulator knows the audio clock's count at any instant, so the report
 * checks that independently of GenAVB:
 *
 *  - error: the audio clock's rate over the last second, against gPTP time,
 *    in ppm (0 when recovered);
 *  - phase: how far the audio clock has drifted from a perfect gPTP-rate
 *    clock since recovery locked, in ns (bounded when recovered), and the
 *    largest drift seen since then.
 */

#include <inttypes.h>
#include <stdbool.h>

#include "paad/genavb/mclk_rec.h"
#include "paad/sim/console.h"
#include "paad/sim/platform.h"

#include "media_clock.h"

static struct {
	bool have_prev;
	uint64_t prev_gptp_ns;
	struct paad_sim_mclk_count prev;

	bool locked; /* reference taken */
	uint32_t resets;   /* GenAVB's count when it was taken */
	bool from_stream;
	uint64_t ref_gptp_ns;
	struct paad_sim_mclk_count ref;
	double phase_max_ns;
} mc;

static const char *const state_names[] = {
	[PAAD_GENAVB_MCLK_REC_STOPPED] = "stopped",
	[PAAD_GENAVB_MCLK_REC_STARTING] = "starting",
	[PAAD_GENAVB_MCLK_REC_MEASURING] = "measuring",
	[PAAD_GENAVB_MCLK_REC_ADJUSTING] = "adjusting",
	[PAAD_GENAVB_MCLK_REC_LOCKED] = "locked",
};

/* b - a, in ticks. */
static double ticks_between(struct paad_sim_mclk_count a, struct paad_sim_mclk_count b)
{
	return (double)(b.ticks - a.ticks) + ((double)b.frac - (double)a.frac) / 4294967296.0;
}

void media_clock_report(const char *tag, unsigned int t, uint64_t source_ns, uint64_t gptp_ns)
{
	struct paad_sim_platform *board = paad_sim_platform();
	struct paad_genavb_mclk_rec_status st;
	struct paad_sim_mclk_count now;
	double hz, error_ppm = 0.0, phase_ns = 0.0;
	bool have_error;

	if (!board || !board->hal.mclk)
		return;

	paad_genavb_mclk_rec_status(&st);
	if (!st.registered)
		return;

	hz = (double)board->mclk.hal.tick_hz;
	now = paad_sim_mclk_count_at(&board->mclk, source_ns);

	have_error = mc.have_prev && gptp_ns > mc.prev_gptp_ns;
	if (have_error)
		error_ppm = (ticks_between(mc.prev, now) * 1e9 / (hz * (double)(gptp_ns - mc.prev_gptp_ns)) - 1.0) * 1e6;
	mc.have_prev = true;
	mc.prev = now;
	mc.prev_gptp_ns = gptp_ns;

	/* A new lock (after a restart, or on another source) starts a new
	 * reference: the recovery does not keep phase across those. */
	if (st.state != PAAD_GENAVB_MCLK_REC_LOCKED || st.resets != mc.resets || st.from_stream != mc.from_stream) {
		mc.locked = false;
		mc.resets = st.resets;
		mc.from_stream = st.from_stream;
	}

	if (st.state != PAAD_GENAVB_MCLK_REC_LOCKED) {
		/* not locked: nothing to measure the phase against */
	} else if (!mc.locked) {
		mc.locked = true;
		mc.ref = now;
		mc.ref_gptp_ns = gptp_ns;
		mc.phase_max_ns = 0.0;
	} else {
		phase_ns = ticks_between(mc.ref, now) * 1e9 / hz - (double)(gptp_ns - mc.ref_gptp_ns);
		if (phase_ns > mc.phase_max_ns)
			mc.phase_max_ns = phase_ns;
		else if (-phase_ns > mc.phase_max_ns)
			mc.phase_max_ns = -phase_ns;
	}

	paad_sim_printf("%s: t=%us media clock: %s to %s, adjust %+.3f ppm, error %+.3f ppm, phase %+.0f ns, "
			"max %.0f ns, resets %" PRIu32 "\n",
			tag, t, state_names[st.state], st.from_stream ? "stream" : "gPTP", st.adjust_ppb / 1000.0,
			have_error ? error_ppm : 0.0, phase_ns, mc.phase_max_ns, st.resets);
}
