/*
 * Copyright 2026 AIASK Limited (Green Light Audio)
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* GenAVB's OS clock layer (rtos/clock.c) on a fake hardware clock whose time
 * the test sets directly, the way openPAAD's HAL port maps it: the gPTP clock
 * and the system monotonic clock share one hardware clock, so adjusting the
 * gPTP frequency makes the monotonic clock compensate in software.
 * Everything else rtos/clock.c needs is stubbed here.
 */

#include <stdarg.h>
#include <stddef.h>

#include "rtos_abstraction_layer.h"

#include "common/log.h"
#include "os/clock.h"
#include "clock.h"
#include "hw_clock.h"
#include "hr_timer.h"
#include "psfp.h"
#include "scheduled_traffic.h"
#include "net_logical_port.h"

#include "genavb_clock_fixture.h"

static struct hw_clock fake_hw_clock;
static uint64_t fake_hw_ns;
static bool initialised;

/* --- Fake hardware clock (1 GHz: cycles are nanoseconds) -------------------- */

struct hw_clock *hw_clock_get(hw_clock_id_t id)
{
	return id == HW_CLOCK_NONE ? NULL : &fake_hw_clock;
}

uint64_t hw_clock_read(struct hw_clock *clk)
{
	(void)clk;
	return fake_hw_ns;
}

uint64_t hw_clock_read_isr(struct hw_clock *clk)
{
	(void)clk;
	return fake_hw_ns;
}

int hw_clock_setfreq(struct hw_clock *clk, int32_t ppb)
{
	(void)clk;
	(void)ppb;
	return 0;
}

int hw_clock_setoffset(struct hw_clock *clk, int64_t offset)
{
	(void)clk;
	(void)offset;
	return 0;
}

int hw_clock_time_to_cycles(struct hw_clock *clk, uint64_t ns, uint64_t *cycles)
{
	(void)clk;
	*cycles = ns;
	return 0;
}

int hw_clock_dtime_to_cycles(struct hw_clock *clk, uint64_t delta_ns, uint64_t *delta_cycles)
{
	(void)clk;
	*delta_cycles = delta_ns;
	return 0;
}

int hw_clock_convert(struct hw_clock *clk_src, uint64_t ns_src, struct hw_clock *clk_dst, uint64_t *ns_dst)
{
	(void)clk_src;
	(void)clk_dst;
	*ns_dst = ns_src;
	return 0;
}

/* --- Rest of GenAVB and the RTOS, unused by these tests ---------------------- */

void hr_timer_clock_discontinuity(os_clock_id_t clk) { (void)clk; }
void psfp_clock_discontinuity(os_clock_id_t clk_id) { (void)clk_id; }
int st_clock_discontinuity(os_clock_id_t clk_id) { (void)clk_id; return 0; }

bool logical_port_valid(unsigned int port) { (void)port; return false; }
bool logical_port_is_endpoint(unsigned int port) { (void)port; return true; }
unsigned int logical_port_endpoint_id(unsigned int port) { return port; }
unsigned int logical_port_bridge_id(unsigned int port) { return port; }

log_level_t log_component_lvl[max_COMPONENT_ID]; /* all zero: errors only */
const char *log_lvl_string[16];

void _os_log(const char *level, const char *func, const char *component, const char *format, ...)
{
	(void)level;
	(void)func;
	(void)component;
	(void)format;
}

rtos_spinlock_key_t rtos_global_key;
rtos_spinlock_t rtos_global_spinlock;

void vPortEnterCritical(void) {}
void vPortExitCritical(void) {}
void vTaskSuspendAll(void) {}
BaseType_t xTaskResumeAll(void) { return pdFALSE; }
void vTaskDelay(const TickType_t ticks) { (void)ticks; }

/* No timekeeping task: as if it never got to run. */
int rtos_thread_create(rtos_thread_t *thread, unsigned int priority, unsigned int affinity,
		       size_t stack_size, const char *name, void (*func)(void *), void *arg)
{
	(void)thread;
	(void)priority;
	(void)affinity;
	(void)stack_size;
	(void)name;
	(void)func;
	(void)arg;
	return 0;
}

void rtos_thread_abort(rtos_thread_t *thread) { (void)thread; }

/* --- Driving the clock layer ------------------------------------------------- */

void gc_set_hw_time(uint64_t ns)
{
	fake_hw_ns = ns;
}

int gc_init(void)
{
	if (initialised)
		return 0;

	if (clock_to_hw_clock_set(OS_CLOCK_GPTP_EP_0_0, HW_CLOCK_PORT_BASE) < 0)
		return -1;

	if (os_clock_init() < 0)
		return -1;

	initialised = true;

	return 0;
}

int gc_set_gptp_freq(int32_t ppb)
{
	return os_clock_setfreq(OS_CLOCK_GPTP_EP_0_0, ppb);
}

int gc_monotonic(uint64_t *ns)
{
	/* The interrupt variant never updates the time base. */
	return os_clock_gettime64_isr(OS_CLOCK_SYSTEM_MONOTONIC, ns);
}

int gc_monotonic_to_hw(uint64_t ns, uint64_t *hw_ns)
{
	return __clock_time_to_cycles(OS_CLOCK_SYSTEM_MONOTONIC, ns, hw_ns);
}

int gc_monotonic_delta_to_hw(uint64_t delta_ns, uint64_t *hw_delta_ns)
{
	return __clock_dtime_to_cycles(OS_CLOCK_SYSTEM_MONOTONIC, delta_ns, hw_delta_ns);
}
