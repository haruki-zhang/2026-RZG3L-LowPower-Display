// SPDX-License-Identifier: BSD-3-Clause
/*
 * Power read-out of the LP picture (Step 4, design_handoff_lpdemo_gui section 7.2).
 *
 * RPF1 holds the template rendered by the GUI (lpdemo_lpimage.render_rpf1()). The CM33 only
 * rewrites two rectangles of it: the power "d.ddd" and the saving "-dd%". The glyphs, their
 * positions and the rectangles come from lp_glyphs.h, generated from the same Python module
 * that renders the template: characters drawn one by one at whole pixel pen positions,
 * advance + letter spacing, so both produce the same pixels.
 *
 * Speed matters: RPF1 is single buffered and the CM33 runs from SRAM (about 10 M simple
 * instructions per second measured on the EVK). So
 *   - glyph boxes start on an even x and one byte of coverage (two pixels) is turned into
 *     one 32-bit store through a 256-entry table;
 *   - while the layout of "d.ddd" stays the same, only the cells whose digit changed are
 *     rewritten, and only the rows of the old and the new glyph;
 *   - the rewrite starts at a frame boundary (sync_wait(), found by sync_probe()): from
 *     there to the first RPF1 line (y = 556 at 1080p) there are more than 8 ms.
 */
#include <stdint.h>
#include "cm33.h"
#include "lp_glyphs.h"
#include "readout.h"
#include "sync.h"
#include "lpdemo_shared.h"

#define SYNC_TIMEOUT_US		(40000u)	/* > 2 frames at 60 Hz */
#define RUN_MAX			(5u)		/* "d.ddd" / "-dd%" */

_Static_assert(((LPG_POWER_CLEAR_X0 | LPG_POWER_CLEAR_X1 | LPG_PCT_CLEAR_X0 |
		 LPG_PCT_CLEAR_X1) & 1) == 0, "32-bit aligned rectangles");
_Static_assert(LPG_LAYER_W * LPG_LAYER_H * 2 <= LPDEMO_SRAM_BAR_SIZE, "RPF1 size");

struct run_font {
	const struct lpg_glyph *glyphs;
	uint32_t n;
	const uint8_t *data;
	const uint32_t *pal;		/* coverage byte (2 pixels) -> 2 RGB565 pixels */
};

struct rect {
	int32_t x0, y0, x1, y1;		/* x0, x1 even */
};

static uint32_t pal_power[256], pal_pct[256];
static uint32_t bg2;			/* two background pixels */

static const struct run_font power_font = {
	lpg_power_glyphs, LPG_POWER_GLYPHS, lpg_power_data, pal_power
};
static const struct run_font pct_font = { lpg_pct_glyphs, LPG_PCT_GLYPHS, lpg_pct_data, pal_pct };

static const struct rect power_rect = {
	LPG_POWER_CLEAR_X0, LPG_POWER_CLEAR_Y0, LPG_POWER_CLEAR_X1, LPG_POWER_CLEAR_Y1
};
static const struct rect pct_rect = {
	LPG_PCT_CLEAR_X0, LPG_PCT_CLEAR_Y0, LPG_PCT_CLEAR_X1, LPG_PCT_CLEAR_Y1
};

static int active;
static uint32_t shown_mw, shown_pct;
static uint32_t px_written;

static uint32_t rgb565(uint32_t rgb)
{
	return ((rgb >> 8) & 0xF800u) | ((rgb >> 5) & 0x07E0u) | ((rgb >> 3) & 0x001Fu);
}

/* pal[b] = two pixels of fg over bg with the coverages (b & 15) / 15, (b >> 4) / 15 */
static void make_palette(uint32_t *pal, uint32_t fg, uint32_t bg)
{
	uint16_t p16[16];

	for (uint32_t a = 0; a < 16u; a++) {
		uint32_t c = 0;

		for (uint32_t sh = 0; sh < 24u; sh += 8u) {
			uint32_t f = (fg >> sh) & 0xFFu, b = (bg >> sh) & 0xFFu;

			c |= ((b * (15u - a) + f * a + 7u) / 15u) << sh;
		}
		p16[a] = (uint16_t)rgb565(c);
	}
	for (uint32_t b = 0; b < 256u; b++)
		pal[b] = p16[b & 15u] | ((uint32_t)p16[b >> 4] << 16);
}

static inline volatile uint32_t *rpf1_word(int32_t x, int32_t y)
{
	return (volatile uint32_t *)(uintptr_t)(LPDEMO_SRAM_BAR_BASE +
						((uint32_t)y * LPG_LAYER_W + (uint32_t)x) * 2u);
}

static const struct lpg_glyph *glyph(const struct run_font *f, uint32_t ch)
{
	for (uint32_t i = 0; i < f->n; i++)
		if (f->glyphs[i].ch == ch)
			return &f->glyphs[i];
	return 0;
}

/* Box of g at pen: r, returns the variant */
static uint32_t glyph_box(const struct lpg_glyph *g, int32_t pen, int32_t baseline,
			  struct rect *r)
{
	uint32_t k = (uint32_t)(pen + g->x[0]) & 1u;

	r->x0 = pen + g->x[k];
	r->x1 = r->x0 + g->w[k];
	r->y0 = baseline + g->y;
	r->y1 = r->y0 + g->h;
	return k;
}

/*
 * Writes the region r (inside the read-out rectangles): the glyph g (may be 0) at pen,
 * background everywhere else. Every pixel of r is written exactly once.
 */
static void draw_region(const struct run_font *f, const struct lpg_glyph *g, int32_t pen,
			int32_t baseline, const struct rect *r)
{
	struct rect b;			/* no initialiser: gcc would call memset() */
	uint32_t k = 0, stride = 0;
	const uint8_t *src = 0;
	int32_t gx0 = r->x0, gx1 = r->x0;	/* glyph columns inside r */

	b.y0 = b.y1 = 0;
	b.x0 = b.x1 = r->x0;
	if (g) {
		k = glyph_box(g, pen, baseline, &b);
		stride = g->w[k] / 2u;
		gx0 = b.x0 > r->x0 ? b.x0 : r->x0;
		gx1 = b.x1 < r->x1 ? b.x1 : r->x1;
		if (gx1 < gx0)
			gx1 = gx0;
	}
	for (int32_t y = r->y0; y < r->y1; y++) {
		volatile uint32_t *dst = rpf1_word(r->x0, y);
		int32_t x = r->x0;

		if (g && y >= b.y0 && y < b.y1 && gx1 > gx0) {
			src = f->data + g->off[k] + (uint32_t)(y - b.y0) * stride +
			      (uint32_t)(gx0 - b.x0) / 2u;
			for (; x < gx0; x += 2)
				*dst++ = bg2;
			for (; x < gx1; x += 2)
				*dst++ = f->pal[*src++];
		}
		for (; x < r->x1; x += 2)
			*dst++ = bg2;
	}
	px_written += (uint32_t)((r->x1 - r->x0) * (r->y1 - r->y0));
}

static void fill(const struct rect *r)
{
	draw_region(0, 0, 0, 0, r);
}

/* A whole run: background over the rectangle, then the glyph boxes */
static void draw_run(const struct run_font *f, const uint16_t *s, uint32_t n, int32_t x,
		     int32_t spacing, int align_right, int32_t baseline, const struct rect *rect)
{
	const struct lpg_glyph *g[RUN_MAX];
	uint32_t m = 0;
	int32_t total = 0;

	for (uint32_t i = 0; i < n && m < RUN_MAX; i++) {
		g[m] = glyph(f, s[i]);
		if (g[m]) {
			total += (int32_t)g[m]->adv + spacing;
			m++;
		}
	}
	if (align_right)
		x -= total;
	fill(rect);
	for (uint32_t i = 0; i < m; i++) {
		struct rect b;

		glyph_box(g[i], x, baseline, &b);
		if (b.x0 >= rect->x0 && b.x1 <= rect->x1 && b.y0 >= rect->y0 && b.y1 <= rect->y1)
			draw_region(f, g[i], x, baseline, &b);
		x += (int32_t)g[i]->adv + spacing;
	}
}

/* The value to show: power_avg_mw, LPDEMO_READOUT_NONE when there is no valid sample */
static uint32_t current_mw(volatile struct lpdemo_ctrl *ctrl)
{
	int32_t mw = ctrl->power_avg_mw;

	if (ctrl->pm_status != LPDEMO_PM_OK || ctrl->pm_count == 0u || mw < 0 || mw > 9999)
		return LPDEMO_READOUT_NONE;
	return (uint32_t)mw;
}

/* round((1 - mw / baseline) * 100), 0..99; LPDEMO_READOUT_NONE without a baseline */
static uint32_t saving_pct(uint32_t mw, uint32_t baseline)
{
	uint32_t pct;

	if (mw == LPDEMO_READOUT_NONE || baseline == 0u || baseline > 100000u)
		return LPDEMO_READOUT_NONE;
	if (mw >= baseline)
		return 0;
	pct = (200u * (baseline - mw) + baseline) / (2u * baseline);
	return pct > 99u ? 99u : pct;
}

static void power_chars(uint32_t mw, uint16_t *s)
{
	if (mw == LPDEMO_READOUT_NONE) {
		s[0] = '-';
		s[1] = '.';
		s[2] = s[3] = s[4] = '-';
		return;
	}
	s[0] = (uint16_t)('0' + mw / 1000u);
	s[1] = '.';
	s[2] = (uint16_t)('0' + mw / 100u % 10u);
	s[3] = (uint16_t)('0' + mw / 10u % 10u);
	s[4] = (uint16_t)('0' + mw % 10u);
}

/*
 * "d.ddd" -> "d.ddd": only the changed cells, and in a cell only the rows of the old and
 * the new glyph (gen_lp_glyphs.py checks that every box fits in its cell).
 */
static void update_power_cells(uint32_t old_mw, uint32_t mw)
{
	uint16_t o[RUN_MAX], s[RUN_MAX];

	power_chars(old_mw, o);
	power_chars(mw, s);
	for (uint32_t i = LPG_POWER_CHARS; i-- > 0u;) {
		const struct lpg_glyph *go = glyph(&power_font, o[i]);
		const struct lpg_glyph *gn = glyph(&power_font, s[i]);
		struct rect r, bo, bn;

		if (o[i] == s[i] || !go || !gn)
			continue;
		glyph_box(go, lpg_power_pen[i], LPG_POWER_BASELINE, &bo);
		glyph_box(gn, lpg_power_pen[i], LPG_POWER_BASELINE, &bn);
		r.x0 = lpg_power_cell[i];
		r.x1 = lpg_power_cell[i + 1u];
		r.y0 = bo.y0 < bn.y0 ? bo.y0 : bn.y0;
		r.y1 = bo.y1 > bn.y1 ? bo.y1 : bn.y1;
		draw_region(&power_font, gn, lpg_power_pen[i], LPG_POWER_BASELINE, &r);
	}
}

static void draw_power(uint32_t mw)
{
	uint16_t s[RUN_MAX];

	power_chars(mw, s);
	draw_run(&power_font, s, 5, LPG_POWER_RIGHT, LPG_POWER_SPACING, 1, LPG_POWER_BASELINE,
		 &power_rect);
}

static void draw_pct(uint32_t pct)
{
	uint16_t s[RUN_MAX];
	uint32_t n = 0;

	if (pct != LPDEMO_READOUT_NONE) {
		s[n++] = (uint16_t)LPG_CH_MINUS;
		if (pct >= 10u)
			s[n++] = (uint16_t)('0' + pct / 10u);
		s[n++] = (uint16_t)('0' + pct % 10u);
		s[n++] = '%';
	}
	draw_run(&pct_font, s, n, LPG_PCT_LEFT, LPG_PCT_SPACING, 0, LPG_PCT_BASELINE, &pct_rect);
}

static void publish(volatile struct lpdemo_ctrl *ctrl, uint32_t t0, uint32_t wait_us)
{
	ctrl->readout_us = (syc_lo() - t0) / (SYC_HZ / 1000000u);
	ctrl->readout_wait_us = wait_us;
	ctrl->readout_px = px_written;
	ctrl->readout_mw = shown_mw;
	ctrl->readout_pct = shown_pct;
	ctrl->readout_count = ctrl->readout_count + 1u;
}

int readout_begin(volatile struct lpdemo_ctrl *ctrl)
{
	uint32_t t0 = syc_lo();
	uint32_t bg = ctrl->bg_rgb & 0xFFFFFFu;

	active = (ctrl->lp_flags & LPDEMO_LP_F_READOUT) && ctrl->bar_w == LPG_LAYER_W &&
		 ctrl->bar_h == LPG_LAYER_H;
	ctrl->readout_mw = LPDEMO_READOUT_NONE;
	ctrl->readout_pct = LPDEMO_READOUT_NONE;
	if (!active)
		return 0;
	make_palette(pal_power, LPG_POWER_RGB, bg);
	make_palette(pal_pct, LPG_PCT_RGB, bg);
	bg2 = pal_power[0];
	shown_mw = current_mw(ctrl);
	shown_pct = saving_pct(shown_mw, ctrl->baseline_mw);
	px_written = 0;
	draw_power(shown_mw);		/* not scanned out yet: no need to wait */
	draw_pct(shown_pct);
	publish(ctrl, t0, 0);
	return 1;
}

void readout_update(volatile struct lpdemo_ctrl *ctrl)
{
	uint32_t mw, pct, wait_us, t0;

	if (!active)
		return;
	mw = current_mw(ctrl);
	pct = saving_pct(mw, ctrl->baseline_mw);
	if (mw == shown_mw && pct == shown_pct)
		return;
	if (sync_wait(SYNC_TIMEOUT_US, &wait_us) != 0)
		ctrl->readout_late = ctrl->readout_late + 1u;
	t0 = syc_lo();
	px_written = 0;
	if (mw != shown_mw) {
		/* "-.---" has other cells than "d.ddd" */
		if (mw != LPDEMO_READOUT_NONE && shown_mw != LPDEMO_READOUT_NONE)
			update_power_cells(shown_mw, mw);
		else
			draw_power(mw);
	}
	if (pct != shown_pct)
		draw_pct(pct);
	shown_mw = mw;
	shown_pct = pct;
	publish(ctrl, t0, wait_us);
}
