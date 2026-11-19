// SPDX-License-Identifier: BSD-3-Clause
/*
 * RZ/G3L LowPower Display Demo - CM33 firmware:
 *   - shared control block in SRAM (lpdemo_shared.h)
 *   - module power (5V_MODULE, ISL28022 on RIIC0) sampled every period_ms (Step 1)
 *   - display hand-over on the running VSPD, return on USER_SW3 (Step 2, vspd.c)
 *   - DISP_RET system suspend: DDR retention exit, display back to Linux, CA55 wake-up
 *     through the MHU (Step 3, wake.c + the TF-A DDR driver)
 *   - LP picture power read-out redrawn in RPF1, LP power samples for the GUI history
 *     (Step 4, readout.c)
 *   - 10 ms SysTick, WFI in between (build with EXTRA_CFLAGS=-DLPDEMO_NO_WFI to poll instead,
 *     e.g. to rule out a wake-up problem when the heartbeat does not advance)
 *   - extension hooks for code linked in from another layer (ext.h)
 *
 * Everything lives in SRAM; the firmware never accesses DDR (an extension may, but not
 * during DISP_RET: ext.h).
 */
#include <stdint.h>
#include "cm33.h"
#include "riic.h"
#include "isl28022.h"
#include "vspd.h"
#include "readout.h"
#include "sync.h"
#include "wake.h"
#include "dbg.h"
#include "ext.h"
#include "lpdemo_shared.h"
#include <ddr.h>

#define FW_VERSION		(0x01030006u)	/* 1.3.6: 0xMMmmpppp = major, minor, 16-bit patch */

#define TICK_MS			(10u)
#define CM33_CLK_HZ_DEFAULT	(200000000u)	/* P3 = PLL2 1600 MHz / 2 / 4 */
#define CM33_CLK_HZ_MIN		(50000000u)
#define CM33_CLK_HZ_MAX		(1000000000u)
#define RETRY_MS		(1000u)		/* after an I2C error */

static volatile struct lpdemo_ctrl *const ctrl =
	(volatile struct lpdemo_ctrl *)LPDEMO_SRAM_CTRL_BASE;

static volatile uint32_t g_ticks;

/*
 * USER_SW3 = SMARC GPIO7 = P65 (port register index 0x26, bit 5), active low.
 * Linux configures the pin (GPIO input with pull-up, rzg3l-smarc-lpdemo.dtsi); the CM33
 * only reads PIN, so it does not touch the pin controller.
 */
#define GPIO_PIN_26		CM33_PERI(0x11030826u)
#define SW3_BIT			(1u << 5)
#define SW3_DEBOUNCE_TICKS	(2u)		/* 20 ms */

static uint32_t sw3_low_ticks;
static int sw3_armed;

/* Returns 1 once per press (after SW3_DEBOUNCE_TICKS low, released before). */
static int sw3_pressed(void)
{
	int low = (mmio_read_8(GPIO_PIN_26) & SW3_BIT) == 0u;

	ctrl->sw3_pin = low ? 0u : 1u;
	if (!low) {
		sw3_low_ticks = 0;
		sw3_armed = 1;
		return 0;
	}
	if (!sw3_armed || ++sw3_low_ticks < SW3_DEBOUNCE_TICKS)
		return 0;
	sw3_armed = 0;		/* wait for the release before the next press */
	return 1;
}

void SysTick_Handler(void)
{
	g_ticks++;
}

/* ext.h defaults, replaced by an extension linked in through EXTRA_SRCS */
__attribute__((weak)) void lpdemo_ext_init(void)
{
}

__attribute__((weak)) void lpdemo_ext_tick(uint32_t now_ms)
{
}

__attribute__((weak)) int lpdemo_ext_display_busy(void)
{
	return 0;
}

void delay_us(uint32_t us)
{
	uint32_t t0 = syc_lo();
	uint32_t n = us * (SYC_HZ / 1000000u);

	while ((syc_lo() - t0) < n)
		;
}

/*
 * Measures the core clock with the DWT cycle counter over 10 ms of SYC. Returns 0 when it
 * cannot be measured: the DWT does not count in secure state unless secure non-invasive
 * debug is enabled.
 */
static uint32_t measure_core_hz(void)
{
	uint32_t c0, t0, hz;

	if (!syc_running())
		return 0;
	mmio_write_32(DEMCR, mmio_read_32(DEMCR) | (1u << 24));	/* TRCENA */
	if (mmio_read_32(DWT_CTRL) & (1u << 25))		/* NOCYCCNT */
		return 0;
	mmio_write_32(DWT_CYCCNT, 0);
	mmio_write_32(DWT_CTRL, mmio_read_32(DWT_CTRL) | 1u);	/* CYCCNTENA */

	t0 = syc_lo();
	c0 = mmio_read_32(DWT_CYCCNT);
	while ((syc_lo() - t0) < SYC_HZ / 100u)
		;
	hz = (mmio_read_32(DWT_CYCCNT) - c0) * 100u;
	return (hz >= CM33_CLK_HZ_MIN && hz <= CM33_CLK_HZ_MAX) ? hz : 0;
}

/*
 * Milliseconds since boot, from SYC (24 MHz): exact whatever the SysTick period really is.
 * SysTick (core clock) only wakes the CPU up. Must be called at least every 178 s.
 */
static uint32_t now_ms_syc(void)
{
	static uint32_t last, acc, ms;
	static int started;
	uint32_t t = syc_lo();

	if (!started) {		/* count from the first call (firmware start) */
		started = 1;
		last = t;
	}
	acc += t - last;
	last = t;
	while (acc >= SYC_HZ / 1000u) {
		acc -= SYC_HZ / 1000u;
		ms++;
	}
	return ms;
}

static void systick_start(uint32_t core_hz)
{
	mmio_write_32(SYST_RVR, core_hz / (1000u / TICK_MS) - 1u);
	mmio_write_32(SYST_CVR, 0);
	mmio_write_32(SYST_CSR, 7u);	/* processor clock, interrupt, enable */
}

static void ctrl_init(uint32_t core_hz)
{
	volatile uint32_t *w = (volatile uint32_t *)ctrl;
	uint32_t i;

	for (i = 0; i < sizeof(struct lpdemo_ctrl) / 4u; i++)
		w[i] = 0;
	ctrl->version = LPDEMO_CTRL_VERSION;
	ctrl->size = sizeof(struct lpdemo_ctrl);
	ctrl->fw_version = FW_VERSION;
	ctrl->cm33_clk_hz = core_hz;
	ctrl->rshunt_uohm = ISL28022_RSHUNT_UOHM;
	ctrl->i2c_addr = ISL28022_ADDR;
	ctrl->pm_status = LPDEMO_PM_NOT_GRANTED;
	ctrl->sw3_wake = 1;
	ctrl->disp_state = LPDEMO_DISP_LINUX;
	ctrl->readout_mw = LPDEMO_READOUT_NONE;
	ctrl->readout_pct = LPDEMO_READOUT_NONE;
	ctrl->ring_period_ms = LPDEMO_RING_PERIOD_MS;
	ctrl->cm33_state = LPDEMO_CM33_WAIT_GRANT;
	dsb();
	ctrl->magic = LPDEMO_CTRL_MAGIC;
	dsb();
}

static inline void pm_begin(void)
{
	ctrl->pm_seq = ctrl->pm_seq + 1u;	/* odd: update in progress */
	dmb();
}

static inline void pm_end(void)
{
	dmb();
	ctrl->pm_seq = ctrl->pm_seq + 1u;	/* even: consistent */
	dsb();
}

static void pm_set_status(uint32_t status, int failed)
{
	if (ctrl->pm_status == status && !failed)
		return;
	pm_begin();
	ctrl->pm_status = status;
	if (failed)
		ctrl->pm_err_count = ctrl->pm_err_count + 1u;
	pm_end();
}

static void pm_publish(const struct isl28022_sample *s, uint64_t syc, int32_t avg)
{
	pm_begin();
	ctrl->pm_status = LPDEMO_PM_OK;
	ctrl->pm_count = ctrl->pm_count + 1u;
	ctrl->pm_syc_lo = (uint32_t)syc;
	ctrl->pm_syc_hi = (uint32_t)(syc >> 32);
	ctrl->vbus_mv = s->vbus_mv;
	ctrl->vshunt_uv = s->vshunt_uv;
	ctrl->current_ma = s->current_ma;
	ctrl->power_mw = s->power_mw;
	ctrl->power_avg_mw = avg;
	ctrl->raw_vbus = s->raw_vbus;
	ctrl->raw_vshunt = s->raw_vshunt;
	pm_end();
}

static uint32_t lp_since_ms;
static int disp_for_suspend;	/* display handed over by the rz-du suspend */
static int show_progress;	/* LPDEMO_LP_F_PROGRESS of the current LP period */

/*
 * Progress of a DISP_RET suspend, shown in the status bar of the LP picture (one segment
 * each, README section 6.4): the screen is the only post-mortem when the system hangs.
 */
#define PROG_HANDED_OVER	(1u << 0)	/* rz-du suspend: display handed over */
#define PROG_BL31_SUSPEND	(1u << 1)	/* BL31 rz_pwr_domain_suspend() */
#define PROG_BL31_SRAM		(1u << 2)	/* BL31 running from SRAM */
#define PROG_CA55_WFI		(1u << 3)	/* CA55 asleep in the WFI loop */
#define PROG_WAKE_TRIGGER	(1u << 4)	/* USER_SW3 / timer */
#define PROG_DDR_RESTORED	(1u << 5)	/* DDR retention exit done (3b) or not needed (3a) */
#define PROG_MHU		(1u << 6)	/* MHU interrupt raised */
#define PROG_CA55_WOKEN		(1u << 7)	/* CA55 out of the WFI loop */
#define PROG_BL31_RESUMED	(1u << 8)	/* BL31 warm boot done, back to Linux */
static uint32_t prog_done, prog_failed;

static void progress_show(void)
{
	static uint32_t shown_done, shown_failed;

	if (prog_done != shown_done || prog_failed != shown_failed) {
		shown_done = prog_done;
		shown_failed = prog_failed;
		dbg_msg("progress done/failed", (prog_failed << 16) | prog_done);
	}
	if (show_progress)
		vspd_bar_progress(prog_done, prog_failed);
}

static void progress_tick(void)
{
	uint32_t st = ctrl->bl31_stage;

	if (st == LPDEMO_BL31_NO_CM33) {
		prog_failed |= PROG_BL31_SUSPEND;
	} else if (st >= LPDEMO_BL31_SUSPEND && st <= LPDEMO_BL31_RESUMED) {
		prog_done |= PROG_BL31_SUSPEND;
		if (st >= LPDEMO_BL31_SRAM)
			prog_done |= PROG_BL31_SRAM;
		if (st >= LPDEMO_BL31_WFI)
			prog_done |= PROG_CA55_WFI;
		if (st >= LPDEMO_BL31_WOKEN)
			prog_done |= PROG_CA55_WOKEN;
		if (st == LPDEMO_BL31_RESUMED)
			prog_done |= PROG_BL31_RESUMED;
	}
	progress_show();
}

static volatile struct lpdemo_trace *const trace =
	(volatile struct lpdemo_trace *)LPDEMO_SRAM_TRACE_BASE;

static void trace_init(void)
{
	if (trace->magic != LPDEMO_TRACE_MAGIC) {
		volatile uint32_t *w = (volatile uint32_t *)trace;

		for (uint32_t i = 0; i < sizeof(*trace) / 4u; i++)
			w[i] = 0;
		trace->magic = LPDEMO_TRACE_MAGIC;
	}
	trace->boot_count = trace->boot_count + 1u;
}

static void wake_stage(uint32_t stage)
{
	trace->cm33_stage = stage;
	dsb();
}

/*
 * LP power samples for the power history of the GUI (LPDEMO_RING_*): one every
 * LPDEMO_RING_PERIOD_MS from the hand-over on.
 */
static volatile uint16_t *const ring =
	(volatile uint16_t *)(LPDEMO_SRAM_CTRL_BASE + LPDEMO_RING_OFFSET);
static uint32_t ring_next_ms, readout_next_ms;
static int probe_pending;	/* frame timing probe at the first LP tick */

static volatile struct lpdemo_probe *const probe =
	(volatile struct lpdemo_probe *)(LPDEMO_SRAM_CTRL_BASE + LPDEMO_PROBE_OFFSET);

/* Called by long running work: BL31 checks that the heartbeat moves */
static void alive(void)
{
	ctrl->heartbeat = g_ticks;
}

static void ring_sample(void)
{
	uint32_t k = ctrl->ring_count;
	int32_t mw = ctrl->power_avg_mw;

	if (k == 0u) {
		uint64_t t = syc_now();

		ctrl->ring_syc0_lo = (uint32_t)t;
		ctrl->ring_syc0_hi = (uint32_t)(t >> 32);
	}
	ring[k % LPDEMO_RING_LEN] = (ctrl->pm_status == LPDEMO_PM_OK && ctrl->pm_count != 0u &&
				     mw >= 0 && mw < (int32_t)LPDEMO_RING_NO_SAMPLE) ?
				    (uint16_t)mw : (uint16_t)LPDEMO_RING_NO_SAMPLE;
	dsb();
	ctrl->ring_count = k + 1u;
}

static uint32_t readout_period_ms(void)
{
	uint32_t p = ctrl->readout_ms;

	if (p == 0u)
		return LPDEMO_READOUT_MS_DEFAULT;
	if (p < LPDEMO_READOUT_MS_MIN)
		return LPDEMO_READOUT_MS_MIN;
	return p > LPDEMO_READOUT_MS_MAX ? LPDEMO_READOUT_MS_MAX : p;
}

/* Every tick while the CM33 owns the display */
static void lp_tick(uint32_t now_ms)
{
	if (probe_pending) {
		probe_pending = 0;
		sync_probe(probe, alive);
		ctrl->readout_sync = sync_method();
	}
	if ((int32_t)(now_ms - ring_next_ms) >= 0) {
		ring_sample();
		ring_next_ms += LPDEMO_RING_PERIOD_MS;
		if ((int32_t)(now_ms - ring_next_ms) >= 0)	/* fell behind */
			ring_next_ms = now_ms + LPDEMO_RING_PERIOD_MS;
	}
	if ((int32_t)(now_ms - readout_next_ms) >= 0) {
		readout_update(ctrl);
		readout_next_ms = now_ms + readout_period_ms();
	}
}

static void disp_return(uint32_t wake)
{
	int ret = vspd_leave(ctrl);

	if (ret != LPDEMO_DISP_OK) {
		ctrl->disp_err_count = ctrl->disp_err_count + 1u;
		return;		/* still in LP: retried on the next request / event */
	}
	ctrl->disp_wake = wake;
	ctrl->leave_count = ctrl->leave_count + 1u;
	dsb();
	ctrl->disp_state = LPDEMO_DISP_LINUX;
	dsb();
}

/*
 * Display hand-over: commands from the CA55, then CM33-initiated returns.
 * sw3: USER_SW3 press event of this tick (sampled once per tick in main()).
 */
static void disp_tick(uint32_t now_ms, int sw3)
{
	static uint32_t ack;
	uint32_t req = ctrl->disp_req_seq;

	if (req != ack) {
		uint32_t cmd = ctrl->disp_cmd, result = LPDEMO_DISP_OK;
		int for_suspend = (cmd & LPDEMO_DISP_CMD_F_SUSPEND) != 0u;

		cmd &= ~LPDEMO_DISP_CMD_F_SUSPEND;
		dmb();
		if (cmd == LPDEMO_DISP_CMD_ENTER) {
			if (lpdemo_ext_display_busy()) {
				result = LPDEMO_DISP_ERR_BUSY;	/* an extension has the display */
			} else if (ctrl->disp_state == LPDEMO_DISP_LINUX) {
				/* RPF1 is not scanned out yet: draw the read-out first */
				readout_begin(ctrl);
				result = (uint32_t)vspd_enter(ctrl);
				if (result == LPDEMO_DISP_OK) {
					ctrl->enter_count = ctrl->enter_count + 1u;
					ctrl->disp_wake = LPDEMO_WAKE_NONE;
					ctrl->disp_state = LPDEMO_DISP_LP;
					lp_since_ms = now_ms;
					ctrl->ring_count = 0;
					ring_next_ms = now_ms;
					readout_next_ms = now_ms + readout_period_ms();
					probe_pending = 1;
					show_progress = !!(ctrl->lp_flags & LPDEMO_LP_F_PROGRESS);
					sw3_armed = 0;	/* a press must start after the hand-over */
					disp_for_suspend = for_suspend;
					prog_done = PROG_HANDED_OVER;
					prog_failed = 0;
					if (for_suspend) {
						ctrl->bl31_stage = 0;	/* BL31 reports again */
						dbg_enable(!!(ctrl->susp_mode &
							      LPDEMO_SUSP_MODE_DEBUG_UART));
						dbg_msg("display handed over for the suspend, mode",
							ctrl->susp_mode);
					}
				} else {
					ctrl->disp_err_count = ctrl->disp_err_count + 1u;
				}
			} else {
				result = LPDEMO_DISP_ERR_STATE;
			}
		} else if (cmd == LPDEMO_DISP_CMD_LEAVE) {
			if (disp_for_suspend)
				dbg_msg("LEAVE from rz-du, display state", ctrl->disp_state);
			disp_for_suspend = 0;
			if (ctrl->disp_state == LPDEMO_DISP_LP) {
				disp_return(LPDEMO_WAKE_HOST);
				if (ctrl->disp_state != LPDEMO_DISP_LINUX)
					result = LPDEMO_DISP_ERR_TIMEOUT;
			}
			/* already returned by USER_SW3 / timer: nothing to do */
		} else {
			result = LPDEMO_DISP_ERR_STATE;
		}
		ctrl->disp_result = result;
		dsb();
		ctrl->disp_ack_seq = req;
		dsb();
		ack = req;
	}

	if (ctrl->disp_state != LPDEMO_DISP_LP)
		return;
	vspd_poll(ctrl);
	lp_tick(now_ms);
	if (disp_for_suspend) {
		progress_tick();
		if ((now_ms / 1000u) != ((now_ms - TICK_MS) / 1000u))
			dbg_putc('.');		/* alive */
		return;		/* the DISP_RET wake-up (susp_tick()) gives the display back */
	}
	if (sw3 && ctrl->sw3_wake)
		disp_return(LPDEMO_WAKE_SW3);
	else if (ctrl->auto_return_ms && (now_ms - lp_since_ms) >= ctrl->auto_return_ms)
		disp_return(LPDEMO_WAKE_AUTO);
}

/*
 * DISP_RET wake-up (design document section 3.2): BL31 has put the CA55 to sleep in SRAM
 * (susp_state SLEEP, or RETENTION with the DDR in self-refresh and PLL4 in standby).
 * On USER_SW3 (or susp_wake_ms): PLL4 on + DDR retention exit, display back to Linux, then
 * MHU interrupt to the CA55, which continues at the BL31 warm entry point.
 */
static void susp_wake(uint32_t state, uint32_t wake)
{
	uint32_t t0, result = LPDEMO_SUSP_OK;

	ctrl->susp_state = LPDEMO_SUSP_WAKING;
	wake_stage(LPDEMO_WAKE_STAGE_TRIGGER);
	dbg_msg("wake-up: trigger (2 = USER_SW3, 3 = timer)", wake);
	prog_done |= PROG_WAKE_TRIGGER;
	progress_show();

	if (state == LPDEMO_SUSP_RETENTION) {
		result = (uint32_t)retcsr_load();
		if (result == LPDEMO_SUSP_OK) {
			t0 = syc_lo();
			result = (uint32_t)pll4_on();
			ctrl->pll4_us = (syc_lo() - t0) / (SYC_HZ / 1000000u);
		}
		if (result != LPDEMO_SUSP_OK)
			goto fail;
		wake_stage(LPDEMO_WAKE_STAGE_PLL4);

		/* TF-A 13.x sequence, the same sources and DDR parameters as BL2 */
		t0 = syc_lo();
		ddr_retention_exit();
		ctrl->ddr_exit_us = (syc_lo() - t0) / (SYC_HZ / 1000000u);
		wake_stage(LPDEMO_WAKE_STAGE_DDR);
	}
	prog_done |= PROG_DDR_RESTORED;
	progress_show();

	/*
	 * The Linux display list and frame buffers are in DDR: only now. With LATE_RETURN the
	 * LP picture stays until the rz-du resume asks for the display (LEAVE).
	 */
	if (ctrl->disp_state == LPDEMO_DISP_LP &&
	    !(ctrl->susp_mode & LPDEMO_SUSP_MODE_LATE_RETURN)) {
		disp_return(wake);
		if (ctrl->disp_state != LPDEMO_DISP_LINUX)
			result = LPDEMO_SUSP_ERR_DISPLAY;	/* rz-du reports it on resume */
		disp_for_suspend = 0;
	}
	wake_stage(LPDEMO_WAKE_STAGE_DISPLAY);

	ctrl->susp_result = result;
	trace->susp_result = result;
	ctrl->wake_count = ctrl->wake_count + 1u;
	dsb();
	ctrl->susp_state = LPDEMO_SUSP_RESUMED;
	dsb();
	mhu_wake_ca55();
	wake_stage(LPDEMO_WAKE_STAGE_MHU);
	dbg_msg("wake-up: MHU raised, result", result);
	dbg_msg("wake-up: MHU_SW_INT_STS4_NS", mhu_wake_status());
	prog_done |= PROG_MHU;
	progress_show();
	return;

fail:
	/*
	 * The DDR cannot be brought back: waking the CA55 up would crash it. Stay in DISP_RET
	 * with the LP picture; the board has to be reset (the trace survives the reset).
	 */
	ctrl->susp_result = result;
	trace->susp_result = result;
	ctrl->susp_state = LPDEMO_SUSP_FAILED;
	dbg_msg("wake-up FAILED, result", result);
	prog_failed |= PROG_DDR_RESTORED;
	progress_show();
	dsb();
}

/* Returns 1 when it handled the USER_SW3 event (sw3). */
static int susp_tick(uint32_t now_ms, int sw3)
{
	static uint32_t since_ms;
	static int seen;
	uint32_t state = ctrl->susp_state;
	uint32_t wake_ms;

	if (state != LPDEMO_SUSP_SLEEP && state != LPDEMO_SUSP_RETENTION) {
		seen = 0;
		return 0;
	}
	if (!seen) {
		/* BL31 just put the CA55 to sleep */
		seen = 1;
		dbg_msg("CA55 asleep, susp_state", state);
		since_ms = now_ms;
		sw3_armed = 0;		/* a press must start in DISP_RET */
		trace->susp_count = ctrl->susp_count;
		trace->susp_mode = ctrl->susp_mode;
		wake_stage(0);
		return 1;
	}
	wake_ms = ctrl->susp_wake_ms;
	if (sw3)
		susp_wake(state, LPDEMO_WAKE_SW3);
	else if (wake_ms && (now_ms - since_ms) >= wake_ms)
		susp_wake(state, LPDEMO_WAKE_AUTO);
	return 1;
}

static uint32_t period_ms(void)
{
	uint32_t p = ctrl->period_ms;

	if (p == 0u)
		return LPDEMO_PERIOD_MS_DEFAULT;
	if (p < LPDEMO_PERIOD_MS_MIN)
		return LPDEMO_PERIOD_MS_MIN;
	if (p > LPDEMO_PERIOD_MS_MAX)
		return LPDEMO_PERIOD_MS_MAX;
	return p;
}

int main(void)
{
	struct isl28022_sample s;
	int32_t hist[LPDEMO_AVG_SAMPLES];
	uint32_t nhist = 0, ihist = 0;
	uint32_t core_hz, last_tick = 0, next_ms = 0;
	int bus_ready = 0;

	core_hz = measure_core_hz();
	trace_init();
	mhu_init();
	ctrl_init(core_hz);			/* 0 reported = not measurable */
	lpdemo_ext_init();
	systick_start(core_hz ? core_hz : CM33_CLK_HZ_DEFAULT);
	if (syc_running())
		(void)now_ms_syc();		/* starts the ms count here */
	irq_enable();

	for (;;) {
		uint32_t now_ms, ticks;
		int ret, sw3;

#ifndef LPDEMO_NO_WFI
		wfi();
#endif
		ticks = g_ticks;
		if (ticks == last_tick)
			continue;
		last_tick = ticks;
		now_ms = syc_running() ? now_ms_syc() : ticks * TICK_MS;
		ctrl->heartbeat = ticks;
		ctrl->uptime_ms = now_ms;
		trace->uptime_ms = now_ms;

		sw3 = sw3_pressed();
		if (susp_tick(now_ms, sw3))
			sw3 = 0;
		disp_tick(now_ms, sw3);
		lpdemo_ext_tick(now_ms);

		if (ctrl->i2c_grant != LPDEMO_I2C_GRANT) {
			/* RIIC0 belongs to the CA55 side (U-Boot) until it is granted */
			if (ctrl->cm33_state != LPDEMO_CM33_WAIT_GRANT)
				ctrl->cm33_state = LPDEMO_CM33_WAIT_GRANT;
			pm_set_status(LPDEMO_PM_NOT_GRANTED, 0);
			bus_ready = 0;
			nhist = 0;
			continue;
		}
		if (ctrl->cm33_state != LPDEMO_CM33_RUN)
			ctrl->cm33_state = LPDEMO_CM33_RUN;

		if ((int32_t)(now_ms - next_ms) < 0)
			continue;

		if (!bus_ready) {
			uint16_t cfg = 0;

			ret = riic_init();
			if (ret == LPDEMO_PM_OK)
				ret = isl28022_init(&cfg);
			ctrl->isl_config = cfg;
			if (ret != LPDEMO_PM_OK) {
				pm_set_status((uint32_t)ret, 1);
				next_ms = now_ms + RETRY_MS;
				continue;
			}
			bus_ready = 1;
			/* first conversion (2 x 64 ms with 128 averages) before the first read */
			next_ms = now_ms + 200u;
			continue;
		}

		ret = isl28022_read(&s);
		if (ret != LPDEMO_PM_OK) {
			pm_set_status((uint32_t)ret, 1);
			bus_ready = 0;
			next_ms = now_ms + RETRY_MS;
			continue;
		}

		hist[ihist] = s.power_mw;
		ihist = (ihist + 1u) % LPDEMO_AVG_SAMPLES;
		if (nhist < LPDEMO_AVG_SAMPLES)
			nhist++;
		{
			int32_t sum = 0;
			uint32_t i;

			for (i = 0; i < nhist; i++)
				sum += hist[i];
			pm_publish(&s, syc_now(), sum / (int32_t)nhist);
		}
		next_ms += period_ms();
		if ((int32_t)(now_ms - next_ms) >= 0)	/* fell behind (period change) */
			next_ms = now_ms + period_ms();
	}
}
