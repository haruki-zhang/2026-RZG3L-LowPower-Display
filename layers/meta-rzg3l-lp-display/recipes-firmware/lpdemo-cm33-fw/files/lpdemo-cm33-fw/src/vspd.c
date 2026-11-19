// SPDX-License-Identifier: BSD-3-Clause
/*
 * Display hand-over on the running VSPD (design document section 2.2 / 2.3).
 *
 * The CM33 only changes the display through VI6_DL_HDR_ADDR0 and the WPF0 interrupt
 * registers, the registers the HWM (9.4.3.2.3) allows to be changed while the VSPD runs.
 * Everything else is written by display lists, exactly like Linux vsp1 does.
 *
 * Linux vsp1 (continuous mode) writes, per frame, the configuration of the entities that
 * are part of its pipeline only (vsp1_du_pipeline_configure()). The registers of an entity
 * that is not in the Linux pipeline (typically RPF1 and its DPR route when Weston uses the
 * primary plane only) are not rewritten when the Linux display list is restored. The CM33
 * therefore reads every register its LP display list writes before the switch and puts
 * the saved values in a "restore" display list, which is run for at least one frame
 * before the Linux display list is restored.
 *
 * Display list format (vsp1_dl.c): extended header (96 bytes, VI6_DL_EXT_CTRL.EXT is set
 * by vsp1_dlm_setup()) + one body of {register offset, value} pairs. The header loops on
 * itself (next_header = itself, auto start), as in Linux continuous mode.
 */
#include <stdint.h>
#include "cm33.h"
#include "vspd.h"
#include "lpdemo_shared.h"

#define VSPD_BASE		CM33_PERI(0x10870000u)
#define VI6_CMD0		(0x0000u)
#define VI6_CMD_UPDHDR		(1u << 4)
#define VI6_WPF0_IRQ_ENB	(0x0048u)
#define VI6_WPF0_IRQ_STA	(0x004Cu)
#define VI6_WPF_IRQ_UND		(1u << 16)
#define VI6_WPF_IRQ_DFE		(1u << 1)
#define VI6_WPF_IRQ_FRE		(1u << 0)
#define VI6_DL_HDR_ADDR0	(0x0104u)
#define VI6_DL_HDR_REF_ADDR0	(0x0130u)

/*
 * WPF0 inputs: an RPF only takes part in the composition when it is active here. Linux
 * (vsp1_wpf.c) and U-Boot only activate RPF0 (sub) + BRS (VIRACT2 master) = 0x02000001.
 */
#define VI6_WPF0_SRCRPF		(0x1000u)
#define WPF_SRCRPF_RPF_ACT_SUB(n)	(1u << ((n) * 2u))
#define WPF_SRCRPF_VIRACT2_MST	(2u << 24)

#define VI6_DPR_RPF_ROUTE(n)	(0x2000u + (n) * 4u)
#define VI6_DPR_NODE_BRS_IN(n)	(38u + (n))

#define VI6_RPF(n, reg)		(0x0300u + (n) * 0x100u + (reg))
#define RPF_SRC_BSIZE		(0x00u)
#define RPF_SRC_ESIZE		(0x04u)
#define RPF_INFMT		(0x08u)
#define RPF_DSWAP		(0x0Cu)
#define RPF_LOC			(0x10u)
#define RPF_ALPH_SEL		(0x14u)
#define RPF_VRTCOL_SET		(0x18u)
#define RPF_MSK_CTRL		(0x1Cu)
#define RPF_CKEY_CTRL		(0x28u)
#define RPF_SRCM_PSTRIDE	(0x34u)
#define RPF_SRCM_ADDR_Y		(0x3Cu)
#define RPF_SRCM_ADDR_C0	(0x40u)
#define RPF_SRCM_ADDR_C1	(0x44u)
#define RPF_MULT_ALPHA		(0x6Cu)

#define RPF_INFMT_RGB565	((1u << 16) | 0x06u)	/* CIPM | RDFMT = RGB565 */
#define RPF_DSWAP_RGB565	(0x0Eu)			/* LLS | LWS | WDS, as Linux */
#define RPF_ALPH_SEL_FIXED	((4u << 28) | (1u << 18))	/* ASEL fixed | AEXT ext */
#define RPF_ALPHA_FF		(0xFFu)

#define VI6_BRS(reg)		(0x3900u + (reg))
#define BRS_VIRRPF_SIZE		(0x04u)
#define BRS_VIRRPF_COL		(0x0Cu)
#define BRS_CTRL(n)		(0x10u + (n) * 8u)
#define BRS_BLD(n)		(0x14u + (n) * 8u)
#define BRS_CTRL_RBC		(1u << 31)
#define BRS_CTRL_DSTSEL_VRPF	(4u << 20)
#define BRS_CTRL_SRCSEL(n)	((n) << 16)
/* Same blending as Linux brx_configure_stream() for non-premultiplied inputs */
#define BRS_BLD_NORMAL		((3u << 28) | (2u << 24) | (3u << 20) | (4u << 16) | 0xFFu)

/* SRAM display lists */
#define DL_LP_HDR		(LPDEMO_SRAM_DL_BASE + 0x000u)
#define DL_LP_BODY		(LPDEMO_SRAM_DL_BASE + 0x100u)
#define DL_RESTORE_HDR		(LPDEMO_SRAM_DL_BASE + 0x400u)
#define DL_RESTORE_BODY		(LPDEMO_SRAM_DL_BASE + 0x500u)
#define DL_HDR_WORDS		(24u)	/* 96-byte extended header */
#define DLH_AUTO_START		(1u << 0)
/* LP list: WPF0 sources + 2 routes + 2 x 14 RPF + 5 BRS = 36 entries */
#define DL_MAX_ENTRIES		(48u)

_Static_assert(DL_LP_BODY + DL_MAX_ENTRIES * 8u <= DL_RESTORE_HDR &&
	       DL_RESTORE_BODY + DL_MAX_ENTRIES * 8u <= LPDEMO_SRAM_DL_BASE + LPDEMO_SRAM_DL_SIZE,
	       "display list bodies");

/* Debug progress bar: the bottom rows of RPF1 (no text there, lpdemo_lpimage.PROGRESS_Y) */
#define PROGRESS_Y		(84u)

#define SWITCH_TIMEOUT_US	(200000u)	/* > 10 frames at 60 Hz */

static inline uint32_t vspd_rd(uint32_t reg) { return mmio_read_32(VSPD_BASE + reg); }
static inline void vspd_wr(uint32_t reg, uint32_t v) { mmio_write_32(VSPD_BASE + reg, v); }

struct dl_entry {
	uint32_t reg;
	uint32_t val;
};

static struct dl_entry g_lp[DL_MAX_ENTRIES];
static uint32_t g_n;
static int g_overflow;		/* an entry did not fit: the list must not be used */

static void add(uint32_t reg, uint32_t val)
{
	if (g_n >= DL_MAX_ENTRIES) {
		g_overflow = 1;
		return;
	}
	g_lp[g_n].reg = reg;
	g_lp[g_n].val = val;
	g_n++;
}

static void add_rpf(uint32_t n, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t addr)
{
	add(VI6_RPF(n, RPF_SRC_BSIZE), (w << 16) | h);
	add(VI6_RPF(n, RPF_SRC_ESIZE), (w << 16) | h);
	add(VI6_RPF(n, RPF_INFMT), RPF_INFMT_RGB565);
	add(VI6_RPF(n, RPF_DSWAP), RPF_DSWAP_RGB565);
	add(VI6_RPF(n, RPF_LOC), (x << 16) | y);
	add(VI6_RPF(n, RPF_ALPH_SEL), RPF_ALPH_SEL_FIXED);
	add(VI6_RPF(n, RPF_VRTCOL_SET), RPF_ALPHA_FF << 24);
	add(VI6_RPF(n, RPF_MULT_ALPHA), RPF_ALPHA_FF);	/* A/P_MMD none, ratio FFh */
	add(VI6_RPF(n, RPF_MSK_CTRL), 0);
	add(VI6_RPF(n, RPF_CKEY_CTRL), 0);
	add(VI6_RPF(n, RPF_SRCM_PSTRIDE), (w * 2u) << 16);
	add(VI6_RPF(n, RPF_SRCM_ADDR_Y), addr);
	add(VI6_RPF(n, RPF_SRCM_ADDR_C0), 0);
	add(VI6_RPF(n, RPF_SRCM_ADDR_C1), 0);
}

static void write_dl(uint32_t hdr, uint32_t body, const struct dl_entry *e, uint32_t n,
		     int saved_values)
{
	volatile uint32_t *h = (volatile uint32_t *)hdr;
	volatile uint32_t *b = (volatile uint32_t *)body;
	uint32_t i;

	for (i = 0; i < n; i++) {
		b[2u * i] = e[i].reg;
		b[2u * i + 1u] = saved_values ? vspd_rd(e[i].reg) : e[i].val;
	}
	for (i = 0; i < DL_HDR_WORDS; i++)
		h[i] = 0;
	h[0] = 0;			/* num_lists = 0: one body (lists[0]) */
	h[1] = n * 8u;			/* lists[0].num_bytes */
	h[2] = body;			/* lists[0].addr */
	h[17] = hdr;			/* next_header = this header */
	h[18] = DLH_AUTO_START;		/* flags */
	dsb();
}

static uint32_t rgb565x2(uint32_t rgb)
{
	uint32_t c = ((rgb >> 8) & 0xF800u) | ((rgb >> 5) & 0x07E0u) | ((rgb >> 3) & 0x001Fu);

	return c | (c << 16);
}

/* RPF1 geometry of the current LP period (bar_w = 0: no RPF1) */
static uint32_t bar_w, bar_h, bar_bg;

/* Last drawn progress; invalidated by vspd_enter() */
static uint32_t bar_done = 0xFFFFFFFFu, bar_failed;

void vspd_bar_progress(uint32_t done, uint32_t failed)
{
	volatile uint32_t *bar = (volatile uint32_t *)LPDEMO_SRAM_BAR_BASE;
	uint32_t seg_w, green = rgb565x2(0x30D060u), red = rgb565x2(0xE04040u);
	uint32_t grey = rgb565x2(0x3A4654u);

	if (bar_w == 0u || bar_h <= PROGRESS_Y || (done == bar_done && failed == bar_failed))
		return;
	bar_done = done;
	bar_failed = failed;
	seg_w = (bar_w / VSPD_BAR_SEGMENTS) & ~1u;	/* 80 px at 720: 78 px + 2 px gap */

	for (uint32_t y = PROGRESS_Y; y < bar_h; y++) {
		for (uint32_t x = 0; x < bar_w; x += 2u) {
			uint32_t seg = x / seg_w, c;

			if (seg >= VSPD_BAR_SEGMENTS || (x % seg_w) >= seg_w - 2u)
				c = bar_bg;
			else if (failed & (1u << seg))
				c = red;
			else if (done & (1u << seg))
				c = green;
			else
				c = grey;
			bar[(y * bar_w + x) / 2u] = c;
		}
	}
}

/* Writes HDR_ADDR0 and waits until the VSPD references the new header. */
static int switch_to(uint32_t hdr, uint32_t *us)
{
	uint32_t t0 = syc_lo();
	uint32_t limit = SWITCH_TIMEOUT_US * (SYC_HZ / 1000000u);

	vspd_wr(VI6_DL_HDR_ADDR0, hdr);
	for (;;) {
		if (!(vspd_rd(VI6_CMD0) & VI6_CMD_UPDHDR) && vspd_rd(VI6_DL_HDR_REF_ADDR0) == hdr)
			break;
		if ((syc_lo() - t0) > limit)
			return LPDEMO_DISP_ERR_TIMEOUT;
	}
	*us = (syc_lo() - t0) / (SYC_HZ / 1000000u);
	return LPDEMO_DISP_OK;
}

int vspd_enter(volatile struct lpdemo_ctrl *ctrl)
{
	uint32_t out = vspd_rd(VI6_BRS(BRS_VIRRPF_SIZE));
	uint32_t out_w = (out >> 16) & 0x1FFFu, out_h = out & 0x1FFFu;
	uint32_t w = ctrl->img_w, h = ctrl->img_h, x = ctrl->img_x, y = ctrl->img_y;
	uint32_t bw = ctrl->bar_w, bh = ctrl->bar_h, bx = ctrl->bar_x, by = ctrl->bar_y;
	uint32_t linux_hdr, us = 0;
	int bar = bw != 0u;
	int ret;

	ctrl->out_size = out;
	if (vspd_rd(VI6_CMD0) & VI6_CMD_UPDHDR)
		return LPDEMO_DISP_ERR_BUSY;
	linux_hdr = vspd_rd(VI6_DL_HDR_REF_ADDR0);
	if (linux_hdr == 0u)
		return LPDEMO_DISP_ERR_NOT_RUNNING;
	/* Layers inside the output and their SRAM areas (sizes checked before multiplying) */
	if (w == 0u || h == 0u || (w & 1u) || w > out_w || h > out_h || x > out_w - w ||
	    y > out_h - h || w * h * 2u > LPDEMO_SRAM_IMG_SIZE)
		return LPDEMO_DISP_ERR_PARAM;
	if (bar && (bh == 0u || (bw & 1u) || bw > out_w || bh > out_h || bx > out_w - bw ||
		    by > out_h - bh || bw * bh * 2u > LPDEMO_SRAM_BAR_SIZE))
		return LPDEMO_DISP_ERR_PARAM;

	/* LP display list */
	g_n = 0;
	g_overflow = 0;
	add(VI6_WPF0_SRCRPF, WPF_SRCRPF_VIRACT2_MST | WPF_SRCRPF_RPF_ACT_SUB(0) |
	    (bar ? WPF_SRCRPF_RPF_ACT_SUB(1) : 0u));
	add(VI6_DPR_RPF_ROUTE(0), VI6_DPR_NODE_BRS_IN(0));
	add(VI6_DPR_RPF_ROUTE(1), VI6_DPR_NODE_BRS_IN(1));
	add_rpf(0, w, h, x, y, LPDEMO_SRAM_IMG_BASE);
	/* No RPF1: route and size it anyway (harmless), BRS input 1 does not blend */
	add_rpf(1, bar ? bw : 2u, bar ? bh : 2u, bar ? bx : 0u, bar ? by : 0u,
		LPDEMO_SRAM_BAR_BASE);
	add(VI6_BRS(BRS_VIRRPF_COL), (0xFFu << 24) | (ctrl->bg_rgb & 0xFFFFFFu));
	add(VI6_BRS(BRS_CTRL(0)), BRS_CTRL_RBC | BRS_CTRL_DSTSEL_VRPF | BRS_CTRL_SRCSEL(0));
	add(VI6_BRS(BRS_BLD(0)), BRS_BLD_NORMAL);
	/* No bar: input 1 does not take part in the blending (ROP NOP), as Linux does */
	add(VI6_BRS(BRS_CTRL(1)), bar ? (BRS_CTRL_RBC | BRS_CTRL_SRCSEL(1)) : BRS_CTRL_SRCSEL(1));
	add(VI6_BRS(BRS_BLD(1)), BRS_BLD_NORMAL);
	if (g_overflow)
		return LPDEMO_DISP_ERR_PARAM;

	/* RPF1 content is the GUI template; only the progress bar (debug) is drawn here */
	bar_w = bar ? bw : 0u;
	bar_h = bar ? bh : 0u;
	bar_bg = rgb565x2(ctrl->bg_rgb);
	bar_done = 0xFFFFFFFFu;

	/* Restore list: the current (Linux) values of every register the LP list writes */
	write_dl(DL_RESTORE_HDR, DL_RESTORE_BODY, g_lp, g_n, 1);
	write_dl(DL_LP_HDR, DL_LP_BODY, g_lp, g_n, 0);

	/*
	 * The VSPD interrupt is level triggered: with the interrupts enabled, frame ends of
	 * the LP list would reach the CA55 (and wake it up from DISP_RET in Step 3).
	 */
	ctrl->linux_hdr = linux_hdr;
	ctrl->saved_irq_enb = vspd_rd(VI6_WPF0_IRQ_ENB);
	vspd_wr(VI6_WPF0_IRQ_ENB, 0);
	dsb();

	ret = switch_to(DL_LP_HDR, &us);
	if (ret != LPDEMO_DISP_OK) {
		/* The VSPD did not take the LP list: put the Linux list back and give up */
		vspd_wr(VI6_DL_HDR_ADDR0, linux_hdr);
		vspd_wr(VI6_WPF0_IRQ_ENB, ctrl->saved_irq_enb);
		return ret;
	}
	ctrl->enter_us = us;
	ctrl->lp_srcrpf = vspd_rd(VI6_WPF0_SRCRPF);
	ctrl->lp_brs_ctrl1 = vspd_rd(VI6_BRS(BRS_CTRL(1)));
	/* Underruns before the switch belong to Linux: start counting from here */
	vspd_wr(VI6_WPF0_IRQ_STA, ~VI6_WPF_IRQ_UND & (VI6_WPF_IRQ_UND | VI6_WPF_IRQ_DFE | VI6_WPF_IRQ_FRE));
	return LPDEMO_DISP_OK;
}

int vspd_leave(volatile struct lpdemo_ctrl *ctrl)
{
	uint32_t us1 = 0, us2 = 0;
	uint32_t clear = ~(VI6_WPF_IRQ_DFE | VI6_WPF_IRQ_FRE) &
			 (VI6_WPF_IRQ_UND | VI6_WPF_IRQ_DFE | VI6_WPF_IRQ_FRE);
	int ret;

	vspd_poll(ctrl);
	/* Linux register values for one frame (at least), then the Linux display list */
	ret = switch_to(DL_RESTORE_HDR, &us1);
	if (ret == LPDEMO_DISP_OK)
		ret = switch_to(ctrl->linux_hdr, &us2);
	if (ret != LPDEMO_DISP_OK)
		return ret;
	ctrl->leave_us = us1 + us2;
	vspd_poll(ctrl);

	/*
	 * Frame ends of the CM33 lists are pending in IRQ_STA (DFE/FRE): clear them, or Linux
	 * would see a stale frame end as soon as the interrupts are enabled.
	 */
	vspd_wr(VI6_WPF0_IRQ_STA, clear);
	vspd_wr(VI6_WPF0_IRQ_STA, clear);
	vspd_wr(VI6_WPF0_IRQ_ENB, ctrl->saved_irq_enb);
	dsb();
	return LPDEMO_DISP_OK;
}

void vspd_poll(volatile struct lpdemo_ctrl *ctrl)
{
	if (vspd_rd(VI6_WPF0_IRQ_STA) & VI6_WPF_IRQ_UND) {
		ctrl->und_count = ctrl->und_count + 1u;
		vspd_wr(VI6_WPF0_IRQ_STA,
			~VI6_WPF_IRQ_UND & (VI6_WPF_IRQ_UND | VI6_WPF_IRQ_DFE | VI6_WPF_IRQ_FRE));
	}
}
