/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * RZ/G3L LowPower Display Demo: contract between the CA55 (Linux) and the CM33 firmware.
 *
 * SRAM layout (design document section 2.4, meta-rz-multi-os TF-A layout):
 *
 *   0x20000-0x59FFF  CM33 firmware (BL22). BL2 loads 192 KiB from boot media offset 0x270000.
 *   0x5A000-0x5A60F  DDR retention CSR snapshot (struct lpdemo_retcsr_hdr + words, BL2) <- Step 3
 *   0x5A800-0x5A8FF  trace of the last suspend, kept across a reset (struct lpdemo_trace) <- Step 3
 *   0x5B000-0x5BFFF  shared control block (struct lpdemo_ctrl)        <- Step 1
 *                    + LP power samples at +0x800 (uint16 ring)        <- Step 4
 *   0x5C000-0x5DFFF  display list pool (CM33)                          <- Step 2
 *   0x5E000-0x7CFFF  LP layer RPF0, static picture (GUI)               <- Step 2/4
 *   0x7D000-0x9BFFF  LP layer RPF1, power read-out (GUI template, CM33 digits) <- Step 4
 *   0x9C000-0x9FFFF  TF-A PARAMS + BL31_SRAM (do not touch)
 *
 * The CA55 maps the control block through UIO (/dev/uio*, name "lpdemo-ctrl") as device
 * memory: access it with aligned 32-bit loads/stores only. Every field is a 32-bit word.
 * The Python mirror of this structure is lpdemo_shm.py (recipes-demo/lpdemo-gui).
 * Keep both in sync and bump LPDEMO_CTRL_VERSION on layout changes.
 */
#ifndef LPDEMO_SHARED_H
#define LPDEMO_SHARED_H

#include <stdint.h>

#define LPDEMO_SRAM_FW_BASE		(0x00020000u)
#define LPDEMO_SRAM_FW_LIMIT		(0x0005A000u)
#define LPDEMO_SRAM_CTRL_BASE		(0x0005B000u)
#define LPDEMO_SRAM_CTRL_SIZE		(0x00001000u)

#define LPDEMO_CTRL_MAGIC		(0x4344504Cu)	/* "LPDC" */
#define LPDEMO_CTRL_VERSION		(4u)

#define LPDEMO_SRAM_DL_BASE		(0x0005C000u)
#define LPDEMO_SRAM_DL_SIZE		(0x00002000u)
#define LPDEMO_SRAM_IMG_BASE		(0x0005E000u)	/* RPF0 */
#define LPDEMO_SRAM_IMG_SIZE		(0x0001F000u)	/* 124 KiB */
#define LPDEMO_SRAM_BAR_BASE		(0x0007D000u)	/* RPF1 */
#define LPDEMO_SRAM_BAR_SIZE		(0x0001F000u)	/* 124 KiB */
#define LPDEMO_SRAM_RETCSR_BASE		(0x0005A000u)
#define LPDEMO_SRAM_TRACE_BASE		(0x0005A800u)

/*
 * DDR retention CSR snapshot, copied by BL2 (TF-A PLAT_SYSTEM_SUSPEND=disp) after the cold
 * boot DDR training: the first LPDEMO_RETCSR_WORDS words of ddr_csr_table[] (TF-A uses
 * retcsr_list_1d 255 + 2d 52 + ctrl 4 = 311 of them), CRC-32 (IEEE) over the words.
 */
#define LPDEMO_RETCSR_MAGIC		(0x52534352u)	/* "RCSR" */
#define LPDEMO_RETCSR_WORDS		(384u)

struct lpdemo_retcsr_hdr {
	uint32_t magic;
	uint32_t words;
	uint32_t crc32;
	uint32_t reserved;
	/* uint32_t data[words] follows */
};

/*
 * Trace of the last suspend/wake-up, for the post-mortem after a hang: the CM33 firmware
 * does not clear it at boot when the magic is valid (SRAM keeps its contents across a reset,
 * best effort). BL31 and the CM33 write their progress here and in the control block.
 */
#define LPDEMO_TRACE_MAGIC		(0x5254504Cu)	/* "LPTR" */

struct lpdemo_trace {
	uint32_t magic;
	uint32_t boot_count;		/* CM33 firmware starts since the trace was created */
	uint32_t bl31_stage;		/* LPDEMO_BL31_* of the last suspend */
	uint32_t cm33_stage;		/* LPDEMO_WAKE_STAGE_* of the last wake-up */
	uint32_t susp_count;
	uint32_t susp_result;
	uint32_t susp_mode;
	uint32_t uptime_ms;		/* CM33 uptime at the last update */
};

/* ctrl.cm33_state */
#define LPDEMO_CM33_BOOT		(0u)	/* initialising */
#define LPDEMO_CM33_WAIT_GRANT		(1u)	/* running, waiting for the CA55 to hand over RIIC0 */
#define LPDEMO_CM33_RUN			(2u)	/* measuring */
#define LPDEMO_CM33_FAULT		(0xEEu)	/* CPU fault, see fault_pc / fault_cfsr */

/*
 * ctrl.i2c_grant: RIIC0 (I2C_PM) is shared with U-Boot, which talks to the PMIC in
 * board_late_init(). The CM33 does not touch RIIC0 until the CA55 writes
 * LPDEMO_I2C_GRANT here (lpdemo-ctl grant, run by lpdemo-cm33-grant.service once Linux is
 * up; Linux itself keeps i2c0 disabled). Writing any other value revokes the grant: the
 * CM33 finishes the current transfer and stops using the bus.
 */
#define LPDEMO_I2C_GRANT		(0x47433249u)	/* "I2CG" */

/* ctrl.pm_status */
#define LPDEMO_PM_OK			(0u)
#define LPDEMO_PM_NOT_GRANTED		(1u)	/* waiting for i2c_grant */
#define LPDEMO_PM_BUS_BUSY		(2u)	/* bus busy / SCL or SDA held low */
#define LPDEMO_PM_NACK			(3u)	/* no ACK from the ISL28022 */
#define LPDEMO_PM_TIMEOUT		(4u)	/* RIIC flag did not come up */
#define LPDEMO_PM_CLK_FAIL		(5u)	/* RIIC0 module clock / reset did not come up */
#define LPDEMO_PM_BAD_CONFIG		(6u)	/* ISL28022 config register read-back mismatch */

/*
 * Display hand-over (Step 2, used by the DU suspend path in Step 3).
 *
 * The CA55 (rz-du driver, lp_display sysfs) writes disp_cmd, then increments disp_req_seq.
 * The CM33 executes the command and sets disp_result, then disp_ack_seq = disp_req_seq.
 * The CM33 also returns the display on its own (USER_SW3 or auto_return_ms); disp_state
 * and disp_wake tell the CA55 about it. LEAVE is then acknowledged at once.
 */
#define LPDEMO_DISP_CMD_NONE		(0u)
#define LPDEMO_DISP_CMD_ENTER		(1u)	/* take the display over (LP display list) */
#define LPDEMO_DISP_CMD_LEAVE		(2u)	/* give the display back to Linux */
/*
 * ENTER for the system suspend (rz-du suspend): the CM33 must not give the display back by
 * itself (USER_SW3 / auto_return_ms) until the wake-up from DISP_RET or a LEAVE: in DISP_RET
 * the Linux display list and frame buffers are in DDR, possibly in self-refresh.
 */
#define LPDEMO_DISP_CMD_F_SUSPEND	(0x100u)

/* ctrl.disp_state */
#define LPDEMO_DISP_LINUX		(0u)	/* Linux display list active */
#define LPDEMO_DISP_LP			(1u)	/* LP display list in SRAM active */

/* ctrl.disp_result */
#define LPDEMO_DISP_OK			(0u)
#define LPDEMO_DISP_ERR_BUSY		(1u)	/* VI6_CMD0.UPDHDR set: a Linux DL is still queued */
#define LPDEMO_DISP_ERR_TIMEOUT		(2u)	/* the VSPD did not take the new display list */
#define LPDEMO_DISP_ERR_STATE		(3u)	/* command not valid in disp_state */
#define LPDEMO_DISP_ERR_PARAM		(4u)	/* LP image parameters out of range */
#define LPDEMO_DISP_ERR_NOT_RUNNING	(5u)	/* VSPD not running (no display list referenced) */

/* ctrl.disp_wake: who ended the last LP period */
#define LPDEMO_WAKE_NONE		(0u)
#define LPDEMO_WAKE_HOST		(1u)	/* LEAVE command */
#define LPDEMO_WAKE_SW3			(2u)	/* USER_SW3 */
#define LPDEMO_WAKE_AUTO		(3u)	/* auto_return_ms elapsed */

/*
 * System suspend with the display kept running (DISP_RET, Step 3).
 *
 * Linux (lpdemo-ctl suspend) writes susp_mode, the rz-du suspend hands the display over
 * (LPDEMO_DISP_CMD_ENTER), BL31 (PLAT_SYSTEM_SUSPEND=disp) puts the CA55 to sleep from SRAM
 * and sets susp_state. On USER_SW3 (or auto_return_ms) the CM33 restores DDR if needed, gives
 * the display back to Linux and wakes the CA55 with MHU software interrupt ch4 (GIC SPI 74).
 */
/* ctrl.susp_mode (CA55 -> BL31/CM33, read at suspend) */
#define LPDEMO_SUSP_MODE_DDR_RET	(1u << 0)	/* 3b: DDR self-refresh + PLL4 standby */
#define LPDEMO_SUSP_MODE_PLAIN_WFI	(1u << 1)	/* WFI without Cortex-A55 Sleep Mode (STBYCTL) */
/*
 * Keep the LP picture (and the progress bar) until Linux takes the display back in the
 * rz-du resume, instead of giving it back before waking the CA55 up (bring-up / debug).
 */
#define LPDEMO_SUSP_MODE_LATE_RETURN	(1u << 2)
/* Progress of BL31 and the CM33 on the console SCIF0 (needs console_suspend=N) */
#define LPDEMO_SUSP_MODE_DEBUG_UART	(1u << 3)

/* ctrl.susp_state */
#define LPDEMO_SUSP_NONE		(0u)	/* running */
#define LPDEMO_SUSP_SLEEP		(1u)	/* BL31: CA55 asleep in SRAM, DDR active (3a) */
#define LPDEMO_SUSP_RETENTION		(2u)	/* BL31: CA55 asleep, DDR in retention (3b) */
#define LPDEMO_SUSP_WAKING		(3u)	/* CM33: wake-up in progress */
#define LPDEMO_SUSP_RESUMED		(4u)	/* CM33: done, MHU interrupt raised */
#define LPDEMO_SUSP_FAILED		(0xEEu)	/* CM33: wake-up failed, see susp_result */

/* ctrl.susp_result (CM33) */
#define LPDEMO_SUSP_OK			(0u)
#define LPDEMO_SUSP_ERR_RETCSR		(1u)	/* snapshot missing or CRC mismatch */
#define LPDEMO_SUSP_ERR_PLL4		(2u)	/* PLL4 did not lock */
#define LPDEMO_SUSP_ERR_DISPLAY		(3u)	/* display could not be given back */

/* ctrl.bl31_stage / trace: progress of BL31 */
#define LPDEMO_BL31_SUSPEND		(0x3101u)	/* rz_pwr_domain_suspend() */
#define LPDEMO_BL31_NO_CM33		(0x31A1u)	/* CM33 not alive: suspend skipped */
#define LPDEMO_BL31_SRAM		(0x3102u)	/* running from SRAM, MMU off */
#define LPDEMO_BL31_DDR_RET		(0x3103u)	/* DDR in retention */
#define LPDEMO_BL31_PLL4_OFF		(0x3104u)	/* PLL4 in standby */
#define LPDEMO_BL31_WFI			(0x3105u)	/* entering the WFI loop */
#define LPDEMO_BL31_WOKEN		(0x3106u)	/* MHU wake-up seen */
#define LPDEMO_BL31_WARM_ENTRY		(0x3107u)	/* jumping to the BL31 warm entry point */
#define LPDEMO_BL31_RESUMED		(0x3108u)	/* rz_pwr_domain_suspend_finish() */

/* trace cm33_stage: progress of the CM33 wake-up */
#define LPDEMO_WAKE_STAGE_TRIGGER	(0x3301u)	/* USER_SW3 / timer */
#define LPDEMO_WAKE_STAGE_PLL4		(0x3302u)	/* PLL4 locked */
#define LPDEMO_WAKE_STAGE_DDR		(0x3303u)	/* DDR retention exit done */
#define LPDEMO_WAKE_STAGE_DISPLAY	(0x3304u)	/* display back to Linux */
#define LPDEMO_WAKE_STAGE_MHU		(0x3305u)	/* MHU interrupt raised */

/* CM33 -> CA55 core 0 wake-up: MHU software interrupt ch4 (SW_CA55core0_INT, GIC SPI 74) */
#define LPDEMO_MHU_SWINT_CH		(4u)
#define LPDEMO_GIC_SPI_WAKE		(74u)

#define LPDEMO_PERIOD_MS_DEFAULT	(500u)
#define LPDEMO_PERIOD_MS_MIN		(100u)
#define LPDEMO_PERIOD_MS_MAX		(10000u)

/*
 * LP picture (Step 4, design_handoff_lpdemo_gui section 7): virtual RPF background
 * (bg_rgb) + RPF0 static picture (img_*) + RPF1 power read-out (bar_*), both RGB565 little
 * endian, stride = width * 2, written by the CA55 before the hand-over.
 * ctrl.lp_flags (CA55 -> CM33, read at ENTER):
 */
#define LPDEMO_LP_F_READOUT		(1u << 0)	/* CM33 redraws the number + saving in RPF1 */
#define LPDEMO_LP_F_PROGRESS		(1u << 1)	/* suspend progress bar, RPF1 rows 84..87 (debug) */

#define LPDEMO_READOUT_MS_DEFAULT	(1000u)
#define LPDEMO_READOUT_MS_MIN		(500u)
#define LPDEMO_READOUT_MS_MAX		(10000u)
#define LPDEMO_READOUT_NONE		(0xFFFFFFFFu)	/* readout_mw / readout_pct: nothing shown */

/*
 * LP power samples (Step 4): while the CM33 owns the display it stores power_avg_mw every
 * LPDEMO_RING_PERIOD_MS in a ring of uint16 (mW, LPDEMO_RING_NO_SAMPLE = no valid sample)
 * at control block + LPDEMO_RING_OFFSET, for the power history of the GUI. Sample k
 * (k = 0 .. ring_count - 1, the last LPDEMO_RING_LEN are kept) was taken at SYC
 * ring_syc0 + k * ring_period_ms * 24000. Restarted at every ENTER. The CA55 reads it with
 * 32-bit loads (two samples per word, sample 2n in the low half).
 */
#define LPDEMO_RING_OFFSET		(0x800u)
#define LPDEMO_RING_LEN			(1024u)
#define LPDEMO_RING_PERIOD_MS		(2000u)
#define LPDEMO_RING_NO_SAMPLE		(0xFFFFu)

/*
 * Frame timing probe (Step 4): RPF1 is single buffered, the CM33 rewrites it right after a
 * frame boundary. The WPF0 frame end flags cannot be used for that: with IRQ_ENB = 0 (needed
 * by the hand-over) DFE is not set and FRE stays set (EVK 2026-10-16). So at every hand-over
 * the CM33 samples status registers of the VSPD, DU and DSI for LPDEMO_PROBE_MS and looks,
 * bit by bit, for a once-per-frame pattern (period 15..18.5 ms):
 *   LEVEL  the bit toggles twice per frame with a short phase (0.15..4 ms)
 *   BURST  the bit only toggles in a short burst (<= 4 ms) once per frame
 *   GAP    the bit toggles all the time but pauses (0.15..4 ms) once per frame
 *   LATCH  a latched status bit, set again once per frame after being cleared
 * The short phase / burst / pause is the vertical blanking (or a pulse at the frame
 * boundary): its start is the frame boundary. Results at control block +
 * LPDEMO_PROBE_OFFSET (lpdemo-ctl lp probe).
 */
#define LPDEMO_PROBE_OFFSET		(0x400u)
#define LPDEMO_PROBE_MAGIC		(0x32425250u)	/* "PRB2" */
#define LPDEMO_PROBE_REGS		(7u)
#define LPDEMO_PROBE_BITS		(12u)
#define LPDEMO_PROBE_MS			(90u)

/* probe.sync / ctrl.readout_sync / probe bit kind: how the redraw finds the frame boundary */
#define LPDEMO_SYNC_NONE		(0u)	/* no usable signal: no wait (may tear) */
#define LPDEMO_SYNC_LEVEL		(1u)
#define LPDEMO_SYNC_BURST		(2u)
#define LPDEMO_SYNC_GAP			(3u)
#define LPDEMO_SYNC_LATCH		(4u)

struct lpdemo_probe_reg {
	uint32_t addr;			/* CA55 address */
	uint32_t first;			/* first value read */
	uint32_t toggled;		/* bits that changed */
	uint32_t changes;		/* reads that differed from the previous one */
};

/* A bit that changed (or, LATCH registers, was set again after being cleared) */
struct lpdemo_probe_bit {
	uint32_t reg_bit;		/* reg index << 8 | bit */
	uint32_t changes;		/* toggles (LATCH registers: times set again) */
	uint32_t bursts;		/* toggles after >= 2 ms without one */
	uint32_t gaps;			/* pauses of 0.15..4 ms */
	uint32_t kind;			/* LPDEMO_SYNC_* pattern found, NONE = none */
	uint32_t period_us;		/* of that pattern */
	uint32_t short_us;		/* short phase / burst / pause length */
	uint32_t level;			/* LEVEL: level of the short phase */
};

struct lpdemo_probe {
	uint32_t magic;			/* LPDEMO_PROBE_MAGIC when valid */
	uint32_t count;			/* probes run (one per hand-over) */
	uint32_t duration_us;
	uint32_t samples;		/* reads of every register */
	/* VI6_WPF0_IRQ_STA clearing test: before, after writing 0 to DFE/FRE (+0, +100 us),
	   after writing 1 to them (+100 us) */
	uint32_t sta_before;
	uint32_t sta_w0c;
	uint32_t sta_w0c_100us;
	uint32_t sta_w1c_100us;
	/* chosen frame boundary signal */
	uint32_t sync;			/* LPDEMO_SYNC_* */
	uint32_t sync_reg;		/* index in reg[] */
	uint32_t sync_bit;
	uint32_t sync_level;		/* LEVEL: level of the short phase */
	uint32_t sync_period_us;
	uint32_t sync_short_us;
	uint32_t nbits;			/* entries in bit[] */
	uint32_t reserved;
	struct lpdemo_probe_reg reg[LPDEMO_PROBE_REGS];
	struct lpdemo_probe_bit bit[LPDEMO_PROBE_BITS];
};

/* Number of samples in power_avg_mw */
#define LPDEMO_AVG_SAMPLES		(4u)

struct lpdemo_ctrl {
	/* 0x00: identification (CM33) */
	uint32_t magic;			/* LPDEMO_CTRL_MAGIC, written last at boot */
	uint32_t version;		/* LPDEMO_CTRL_VERSION */
	uint32_t size;			/* sizeof(struct lpdemo_ctrl) */
	uint32_t fw_version;		/* 0xMMmmpppp */

	/* 0x10: CM33 status (CM33) */
	uint32_t cm33_state;		/* LPDEMO_CM33_* */
	uint32_t heartbeat;		/* incremented every SysTick (nominally 10 ms) */
	uint32_t cm33_clk_hz;		/* CM33 core clock measured against SYC at boot, 0 = not measurable (200 MHz assumed) */
	uint32_t uptime_ms;		/* since the CM33 firmware started */

	/* 0x20: CA55 -> CM33 */
	uint32_t i2c_grant;		/* LPDEMO_I2C_GRANT = RIIC0 handed over to the CM33 */
	uint32_t period_ms;		/* sampling period, 0 = LPDEMO_PERIOD_MS_DEFAULT */
	uint32_t baseline_mw;		/* normal-mode power, written by the GUI (Step 3/4) */
	uint32_t susp_wake_ms;		/* != 0: wake up from DISP_RET after this time (tests) */

	/*
	 * 0x30: power measurement (CM33), protected by pm_seq:
	 * the CM33 makes pm_seq odd, updates the fields below, then makes it even again.
	 * Readers retry while pm_seq is odd or changed during the read.
	 */
	uint32_t pm_seq;
	uint32_t pm_status;		/* LPDEMO_PM_*, status of the last attempt */
	uint32_t pm_count;		/* successful samples */
	uint32_t pm_err_count;		/* failed attempts */
	uint32_t pm_syc_lo;		/* SYC (24 MHz, = CA55 CNTVCT) at the sample, low word */
	uint32_t pm_syc_hi;		/* high word */
	int32_t vbus_mv;		/* 5V_MODULE bus voltage */
	int32_t vshunt_uv;		/* voltage across R94 */
	int32_t current_ma;
	int32_t power_mw;		/* vbus * current of the last sample */
	int32_t power_avg_mw;		/* mean of the last LPDEMO_AVG_SAMPLES samples */
	uint32_t raw_vbus;		/* ISL28022 register 02h */
	uint32_t raw_vshunt;		/* ISL28022 register 01h */
	uint32_t isl_config;		/* ISL28022 register 00h (read back) */
	uint32_t rshunt_uohm;		/* shunt resistor used for the calculation */
	uint32_t i2c_addr;		/* ISL28022 7-bit address */

	/* 0x70: diagnostics (CM33) */
	uint32_t fault_pc;
	uint32_t fault_cfsr;
	uint32_t fault_lr;
	uint32_t reserved0;

	/* 0x80: display hand-over, CA55 -> CM33 */
	uint32_t disp_cmd;		/* LPDEMO_DISP_CMD_* */
	uint32_t disp_req_seq;		/* incremented after disp_cmd is written */
	uint32_t auto_return_ms;	/* != 0: return to Linux after this time in LP (tests) */
	uint32_t sw3_wake;		/* != 0: USER_SW3 returns to Linux (default 1) */

	/* 0x90: LP picture, CA55 -> CM33 (read at ENTER), continued at 0x100 */
	uint32_t img_w;			/* RPF0 layer in SRAM, RGB565, stride = img_w * 2 */
	uint32_t img_h;
	uint32_t bg_rgb;		/* background (virtual RPF) colour 0xRRGGBB */
	uint32_t lp_flags;		/* LPDEMO_LP_F_* (v3: bar_rgb) */

	/* 0xA0: display hand-over, CM33 -> CA55 */
	uint32_t disp_ack_seq;		/* = disp_req_seq when the command is done */
	uint32_t disp_result;		/* LPDEMO_DISP_OK / _ERR_* of the last command */
	uint32_t disp_state;		/* LPDEMO_DISP_LINUX / _LP */
	uint32_t disp_wake;		/* LPDEMO_WAKE_*: what ended the last LP period */

	/* 0xB0: display hand-over diagnostics (CM33) */
	uint32_t linux_hdr;		/* VI6_DL_HDR_REF_ADDR0 saved at ENTER */
	uint32_t saved_irq_enb;		/* VI6_WPF0_IRQ_ENB saved at ENTER */
	uint32_t out_size;		/* output size (BRS virtual RPF) w << 16 | h */
	uint32_t enter_count;
	uint32_t leave_count;
	uint32_t disp_err_count;
	uint32_t und_count;		/* WPF0 underruns seen while the CM33 owned the display */
	uint32_t enter_us;		/* last ENTER: HDR_ADDR write -> LP DL referenced */
	uint32_t leave_us;		/* last return: restore DL write -> Linux DL referenced */
	uint32_t sw3_pin;		/* last P65 sample (bit 0, 0 = pressed) */
	uint32_t reserved1[2];

	/* 0xE0: system suspend with the display kept running (Step 3) */
	uint32_t susp_mode;		/* LPDEMO_SUSP_MODE_*, CA55 -> BL31/CM33 */
	uint32_t susp_state;		/* LPDEMO_SUSP_*, BL31 / CM33 */
	uint32_t susp_count;		/* BL31: suspends that reached the SRAM sleep */
	uint32_t wake_count;		/* CM33: completed wake-ups */
	uint32_t susp_result;		/* CM33: LPDEMO_SUSP_OK / _ERR_* of the last wake-up */
	uint32_t pll4_us;		/* last wake-up: PLL4 lock time */
	uint32_t ddr_exit_us;		/* last wake-up: ddr_retention_exit() time */
	uint32_t bl31_stage;		/* LPDEMO_BL31_* */

	/* 0x100: LP picture placement, CA55 -> CM33 (read at ENTER, Step 4) */
	uint32_t img_x;			/* RPF0 position on the screen */
	uint32_t img_y;
	uint32_t bar_w;			/* RPF1 layer, 0 = no RPF1 */
	uint32_t bar_h;
	uint32_t bar_x;			/* RPF1 position on the screen */
	uint32_t bar_y;
	uint32_t readout_ms;		/* RPF1 redraw period, 0 = LPDEMO_READOUT_MS_DEFAULT */

	/* 0x11C: power read-out (CM33) */
	uint32_t readout_count;		/* redraws */
	uint32_t readout_us;		/* last redraw: drawing time */
	uint32_t readout_wait_us;	/* last redraw: wait for the frame end */
	uint32_t readout_late;		/* redraws without a frame end seen (may tear) */
	uint32_t readout_mw;		/* value shown, LPDEMO_READOUT_NONE = "-.---" */
	uint32_t readout_pct;		/* saving shown, LPDEMO_READOUT_NONE = none */

	/* 0x134: LP power samples (CM33), see LPDEMO_RING_* */
	uint32_t ring_count;
	uint32_t ring_period_ms;
	uint32_t ring_syc0_lo;
	uint32_t ring_syc0_hi;

	/* 0x144: LP display list as the VSPD runs it (CM33, read back after ENTER) */
	uint32_t lp_srcrpf;		/* VI6_WPF0_SRCRPF: RPF0/RPF1 active? */
	uint32_t lp_brs_ctrl1;		/* VI6_BRS_CTRL(1): RPF1 blended? */
	uint32_t readout_px;		/* last redraw: pixels written */
	/* 0x150 */
	uint32_t readout_sync;		/* LPDEMO_SYNC_* used by the redraws */
	uint32_t reserved2[3];
};

#define LPDEMO_CTRL_SIZE_V4		(0x160u)

/* lpdemo_shm.py and the rz-du driver rely on these offsets */
_Static_assert(sizeof(struct lpdemo_ctrl) == LPDEMO_CTRL_SIZE_V4, "lpdemo_ctrl size");
_Static_assert(__builtin_offsetof(struct lpdemo_ctrl, i2c_grant) == 0x20, "i2c_grant");
_Static_assert(__builtin_offsetof(struct lpdemo_ctrl, pm_seq) == 0x30, "pm_seq");
_Static_assert(__builtin_offsetof(struct lpdemo_ctrl, power_avg_mw) == 0x58, "power_avg_mw");
_Static_assert(__builtin_offsetof(struct lpdemo_ctrl, fault_pc) == 0x70, "fault_pc");
_Static_assert(__builtin_offsetof(struct lpdemo_ctrl, disp_cmd) == 0x80, "disp_cmd");
_Static_assert(__builtin_offsetof(struct lpdemo_ctrl, img_w) == 0x90, "img_w");
_Static_assert(__builtin_offsetof(struct lpdemo_ctrl, disp_ack_seq) == 0xA0, "disp_ack_seq");
_Static_assert(__builtin_offsetof(struct lpdemo_ctrl, linux_hdr) == 0xB0, "linux_hdr");
/* TF-A (lpdemo_disp.h) relies on these too */
_Static_assert(__builtin_offsetof(struct lpdemo_ctrl, susp_mode) == 0xE0, "susp_mode");
_Static_assert(__builtin_offsetof(struct lpdemo_ctrl, bl31_stage) == 0xFC, "bl31_stage");
_Static_assert(__builtin_offsetof(struct lpdemo_ctrl, img_x) == 0x100, "img_x");
_Static_assert(__builtin_offsetof(struct lpdemo_ctrl, readout_count) == 0x11C, "readout_count");
_Static_assert(__builtin_offsetof(struct lpdemo_ctrl, ring_count) == 0x134, "ring_count");
_Static_assert(sizeof(struct lpdemo_ctrl) <= LPDEMO_PROBE_OFFSET &&
	       LPDEMO_PROBE_OFFSET + sizeof(struct lpdemo_probe) <= LPDEMO_RING_OFFSET, "probe");
_Static_assert(sizeof(struct lpdemo_ctrl) <= LPDEMO_RING_OFFSET &&
	       LPDEMO_RING_OFFSET + LPDEMO_RING_LEN * 2u <= LPDEMO_SRAM_CTRL_SIZE, "ring");
_Static_assert(LPDEMO_SRAM_IMG_BASE + LPDEMO_SRAM_IMG_SIZE == LPDEMO_SRAM_BAR_BASE &&
	       LPDEMO_SRAM_BAR_BASE + LPDEMO_SRAM_BAR_SIZE <= 0x0009C000u, "LP layers");
_Static_assert(LPDEMO_SRAM_RETCSR_BASE + sizeof(struct lpdemo_retcsr_hdr) +
	       LPDEMO_RETCSR_WORDS * 4u <= LPDEMO_SRAM_TRACE_BASE, "retcsr snapshot size");

#endif /* LPDEMO_SHARED_H */
