// SPDX-License-Identifier: BSD-3-Clause
/*
 * Frame boundary for the single buffered RPF1 redraw (Step 4).
 *
 * The WPF0 frame end flags would be the natural choice, but with VI6_WPF0_IRQ_ENB = 0
 * (which the hand-over needs) DFE is not set and FRE stays set (EVK). Rather than relying on
 * register semantics we cannot check, sync_probe() measures: it samples status registers of
 * the VSPD, DU and DSI and looks, bit by bit, for a pattern that repeats once per frame
 * (lpdemo_shared.h, LPDEMO_SYNC_*). The short phase of that pattern is the vertical blanking
 * or a pulse at the frame boundary; its start is between the end of the active area and the
 * first line, so from there the scan-out needs > 8 ms to reach RPF1 (y = 556 at 1080p).
 *
 * EVK, first probe (firmware 1.3.4): DU_MSR0 bits 16/20 toggle at about the line rate, DSI
 * LINKSR bit 12 (HSBUSY) about 24 times per frame, VI6_DISP_IRQ_STA is latched (0x123).
 */
#include <stdint.h>
#include "cm33.h"
#include "sync.h"
#include "lpdemo_shared.h"

#define TICKS_PER_US		(SYC_HZ / 1000000u)
#define US(n)			((n) * TICKS_PER_US)
#define PERIOD_MIN_US		(15000u)
#define PERIOD_MAX_US		(18500u)
#define SHORT_MIN_US		(150u)
#define SHORT_MAX_US		(4000u)
#define BURST_QUIET_US		(2000u)	/* a toggle after this long without one starts a burst */
#define JITTER_US		(500u)
#define EVENTS			(6u)

#define VI6_WPF0_IRQ_STA	(0x1087004Cu)
#define WPF_IRQ_UND		(1u << 16)
#define WPF_IRQ_DFE		(1u << 1)
#define WPF_IRQ_FRE		(1u << 0)

/* In priority order (CA55 addresses) */
static const uint32_t probe_addr[LPDEMO_PROBE_REGS] = {
	0x10870038u,	/* VSPD VI6_STATUS */
	0x1087004Cu,	/* VSPD VI6_WPF0_IRQ_STA (latched) */
	0x1087007Cu,	/* VSPD VI6_DISP_IRQ_STA (latched) */
	0x10890004u,	/* DU_MSR0 */
	0x10890008u,	/* DU_MSR1 */
	0x10860010u,	/* MIPI DSI LINKSR */
	0x10860410u,	/* MIPI DSI VICH1SR */
};

/*
 * Latched status registers: written 0 clears a bit, 1 leaves it alone (vsp1_drv.c). Their
 * bits are cleared after every set, the pattern is the time between sets.
 */
#define LATCH_REGS		((1u << 1) | (1u << 2))
static const uint32_t latch_keep[LPDEMO_PROBE_REGS] = {
	0, WPF_IRQ_UND | WPF_IRQ_DFE | WPF_IRQ_FRE, 0x1FFu, 0, 0, 0, 0
};

struct bit_state {
	uint32_t count;			/* toggles, or sets of a latched bit */
	uint32_t last;			/* time of the last toggle */
	uint32_t armed;			/* latched bit: seen cleared */
	uint32_t edge_n, edge[EVENTS];
	uint32_t burst_n, burst[EVENTS], burst_len[EVENTS];
	uint32_t gap_n, gap[EVENTS], gap_len[EVENTS];
};

static struct bit_state st[LPDEMO_PROBE_REGS][32];

/* Chosen signal */
static uint32_t method = LPDEMO_SYNC_NONE;
static uint32_t sync_addr, sync_mask, sync_level, sync_keep, sync_short_us;

static inline uint32_t rd(uint32_t ca55_addr)
{
	return mmio_read_32(CM33_PERI(ca55_addr));
}

static inline void wr(uint32_t ca55_addr, uint32_t v)
{
	mmio_write_32(CM33_PERI(ca55_addr), v);
}

static void sta_clear_test(volatile struct lpdemo_probe *p)
{
	uint32_t t;

	p->sta_before = rd(VI6_WPF0_IRQ_STA);
	wr(VI6_WPF0_IRQ_STA, WPF_IRQ_UND);		/* 0 = clear DFE/FRE */
	dsb();
	p->sta_w0c = rd(VI6_WPF0_IRQ_STA);
	t = syc_lo();
	while ((syc_lo() - t) < US(100u))
		;
	p->sta_w0c_100us = rd(VI6_WPF0_IRQ_STA);
	wr(VI6_WPF0_IRQ_STA, WPF_IRQ_DFE | WPF_IRQ_FRE);
	dsb();
	t = syc_lo();
	while ((syc_lo() - t) < US(100u))
		;
	p->sta_w1c_100us = rd(VI6_WPF0_IRQ_STA);
}

static uint32_t absdiff(uint32_t a, uint32_t b)
{
	return a > b ? a - b : b - a;
}

/* n >= 4 event times (ticks) one frame period apart: returns the period (us), else 0 */
static uint32_t frame_periodic(const uint32_t *t, uint32_t n)
{
	uint32_t d0, sum = 0;

	if (n > EVENTS)
		n = EVENTS;
	if (n < 4u)
		return 0;
	d0 = (t[1] - t[0]) / TICKS_PER_US;
	for (uint32_t i = 1; i < n; i++) {
		uint32_t d = (t[i] - t[i - 1u]) / TICKS_PER_US;

		if (d < PERIOD_MIN_US || d > PERIOD_MAX_US || absdiff(d, d0) > JITTER_US)
			return 0;
		sum += d;
	}
	return sum / (n - 1u);
}

/* Mean of n lengths (ticks) if all are short phases, else 0 */
static uint32_t short_mean(const uint32_t *len, uint32_t n)
{
	uint32_t sum = 0;

	if (n > EVENTS)
		n = EVENTS;
	if (n == 0u)
		return 0;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t us = len[i] / TICKS_PER_US;

		if (us < SHORT_MIN_US || us > SHORT_MAX_US)
			return 0;
		sum += us;
	}
	return sum / n;
}

/* LEVEL: edges t[0..5], phases A = t1-t0, t3-t2, t5-t4 and B = t2-t1, t4-t3 */
static int level_pattern(const struct bit_state *s, uint32_t level0,
			 volatile struct lpdemo_probe_bit *r)
{
	uint32_t d[EVENTS - 1u], a, b, shrt;

	if (s->edge_n < EVENTS || s->count > 2u * LPDEMO_PROBE_MS / 14u + 2u)
		return 0;
	for (uint32_t i = 0; i < EVENTS - 1u; i++)
		d[i] = (s->edge[i + 1u] - s->edge[i]) / TICKS_PER_US;
	if (absdiff(d[0], d[2]) > JITTER_US || absdiff(d[2], d[4]) > JITTER_US ||
	    absdiff(d[1], d[3]) > JITTER_US)
		return 0;
	a = (d[0] + d[2] + d[4]) / 3u;
	b = (d[1] + d[3]) / 2u;
	shrt = a < b ? a : b;
	if (a + b < PERIOD_MIN_US || a + b > PERIOD_MAX_US || shrt < SHORT_MIN_US ||
	    shrt > SHORT_MAX_US)
		return 0;
	r->period_us = a + b;
	r->short_us = shrt;
	r->level = (a < b) ? !level0 : level0;	/* after edge 0 the bit is !level0 (A) */
	return 1;
}

/* The best pattern of one bit, LPDEMO_SYNC_NONE if none */
static uint32_t classify(uint32_t reg, const struct bit_state *s, uint32_t level0,
			 volatile struct lpdemo_probe_bit *r)
{
	uint32_t period, shrt;

	if ((LATCH_REGS >> reg) & 1u) {
		/* sets of a latched bit: edge[] */
		period = frame_periodic(s->edge, s->edge_n);
		if (period) {
			r->period_us = period;
			r->short_us = 0;
			return LPDEMO_SYNC_LATCH;
		}
		return LPDEMO_SYNC_NONE;
	}
	if (level_pattern(s, level0, r))
		return LPDEMO_SYNC_LEVEL;
	/* bursts: the last one may still be running, its length is unknown */
	period = frame_periodic(s->burst, s->burst_n);
	shrt = s->burst_n > 1u ? short_mean(s->burst_len, s->burst_n - 1u) : 0u;
	if (period && shrt) {
		r->period_us = period;
		r->short_us = shrt;
		return LPDEMO_SYNC_BURST;
	}
	period = frame_periodic(s->gap, s->gap_n);
	shrt = short_mean(s->gap_len, s->gap_n);
	if (period && shrt) {
		r->period_us = period;
		r->short_us = shrt;
		return LPDEMO_SYNC_GAP;
	}
	return LPDEMO_SYNC_NONE;
}

static void toggled_bit(struct bit_state *s, uint32_t now)
{
	uint32_t quiet = now - s->last;

	if (s->edge_n < EVENTS)
		s->edge[s->edge_n] = now;
	s->edge_n++;
	if (quiet >= US(BURST_QUIET_US)) {
		if (s->burst_n > 0u && s->burst_n <= EVENTS)
			s->burst_len[s->burst_n - 1u] = s->last - s->burst[s->burst_n - 1u];
		if (s->burst_n < EVENTS)
			s->burst[s->burst_n] = now;
		s->burst_n++;
	}
	if (quiet >= US(SHORT_MIN_US) && quiet <= US(SHORT_MAX_US)) {
		if (s->gap_n < EVENTS) {
			s->gap[s->gap_n] = s->last;
			s->gap_len[s->gap_n] = quiet;
		}
		s->gap_n++;
	}
	s->last = now;
	s->count++;
}

void sync_probe(volatile struct lpdemo_probe *p, void (*alive)(void))
{
	uint32_t prev[LPDEMO_PROBE_REGS], t0, now, n = 0, nb = 0;
	const uint32_t limit = US(LPDEMO_PROBE_MS * 1000u);

	p->magic = 0;
	sta_clear_test(p);

	t0 = syc_lo();
	for (uint32_t i = 0; i < LPDEMO_PROBE_REGS; i++) {
		for (uint32_t b = 0; b < 32u; b++) {
			struct bit_state *s = &st[i][b];

			s->count = s->armed = s->edge_n = s->burst_n = s->gap_n = 0;
			s->last = t0;
		}
		prev[i] = rd(probe_addr[i]);
		p->reg[i].addr = probe_addr[i];
		p->reg[i].first = prev[i];
		p->reg[i].toggled = 0;
		p->reg[i].changes = 0;
		if ((LATCH_REGS >> i) & 1u)
			wr(probe_addr[i], latch_keep[i] & ~0x3FFu);	/* clear all (UND kept) */
	}
	while ((now = syc_lo()) - t0 < limit) {
		for (uint32_t i = 0; i < LPDEMO_PROBE_REGS; i++) {
			uint32_t v = rd(probe_addr[i]), x;

			if ((LATCH_REGS >> i) & 1u) {
				/* a set bit after it was seen cleared: one event, clear it again */
				for (x = latch_keep[i] & ~WPF_IRQ_UND; x; x &= x - 1u) {
					uint32_t b = (uint32_t)__builtin_ctz(x);
					struct bit_state *s = &st[i][b];

					if (!(v & (1u << b))) {
						s->armed = 1;
					} else if (s->armed) {
						s->armed = 0;
						if (s->edge_n < EVENTS)
							s->edge[s->edge_n] = now;
						s->edge_n++;
						s->count++;
						p->reg[i].changes = p->reg[i].changes + 1u;
						p->reg[i].toggled = p->reg[i].toggled | (1u << b);
						wr(probe_addr[i], latch_keep[i] & ~(1u << b));
					}
				}
				continue;
			}
			x = v ^ prev[i];
			if (!x)
				continue;
			prev[i] = v;
			p->reg[i].changes = p->reg[i].changes + 1u;
			p->reg[i].toggled = p->reg[i].toggled | x;
			for (; x; x &= x - 1u)
				toggled_bit(&st[i][__builtin_ctz(x)], now);
		}
		if ((++n & 0x3FFu) == 0u)
			alive();
	}
	p->duration_us = (syc_lo() - t0) / TICKS_PER_US;
	p->samples = n;

	method = LPDEMO_SYNC_NONE;
	p->sync = LPDEMO_SYNC_NONE;
	for (uint32_t i = 0; i < LPDEMO_PROBE_REGS; i++) {
		for (uint32_t b = 0; b < 32u; b++) {
			struct bit_state *s = &st[i][b];
			volatile struct lpdemo_probe_bit *r;
			uint32_t kind;

			if (!s->count || nb >= LPDEMO_PROBE_BITS)
				continue;
			r = &p->bit[nb++];
			r->reg_bit = (i << 8) | b;
			r->changes = s->count;
			r->bursts = s->burst_n;
			r->gaps = s->gap_n;
			r->period_us = r->short_us = r->level = 0;
			kind = classify(i, s, (p->reg[i].first >> b) & 1u, r);
			r->kind = kind;
			/* LEVEL > BURST > GAP > LATCH, then register order */
			if (kind != LPDEMO_SYNC_NONE &&
			    (method == LPDEMO_SYNC_NONE || kind < method)) {
				method = kind;
				sync_addr = probe_addr[i];
				sync_mask = 1u << b;
				sync_level = r->level;
				sync_keep = latch_keep[i];
				sync_short_us = r->short_us;
				p->sync = kind;
				p->sync_reg = i;
				p->sync_bit = b;
				p->sync_level = r->level;
				p->sync_period_us = r->period_us;
				p->sync_short_us = r->short_us;
			}
		}
	}
	p->nbits = nb;
	p->count = p->count + 1u;
	dsb();
	p->magic = LPDEMO_PROBE_MAGIC;
}

uint32_t sync_method(void)
{
	return method;
}

int sync_wait(uint32_t timeout_us, uint32_t *us)
{
	uint32_t t0 = syc_lo(), limit = US(timeout_us), last_t = t0, now = t0;
	uint32_t last, gap;
	int seen = 0;

	*us = 0;
	if (method == LPDEMO_SYNC_NONE)
		return -1;		/* sync_addr is not set */
	last = rd(sync_addr) & sync_mask;
	if (method == LPDEMO_SYNC_LATCH)
		wr(sync_addr, sync_keep & ~sync_mask);
	/* GAP: a pause this long means the blanking has started */
	gap = sync_short_us / 3u;
	gap = gap < 60u ? 60u : (gap > 300u ? 300u : gap);

	for (;;) {
		uint32_t v = rd(sync_addr) & sync_mask;

		now = syc_lo();
		switch (method) {
		case LPDEMO_SYNC_LEVEL:
			if ((v != 0u) != (sync_level != 0u))
				seen = 1;
			else if (seen)
				goto boundary;	/* the short phase starts */
			break;
		case LPDEMO_SYNC_BURST:
			if (v != last) {
				if (now - last_t >= US(BURST_QUIET_US))
					goto boundary;	/* first toggle of the burst */
				last = v;
				last_t = now;
			}
			break;
		case LPDEMO_SYNC_GAP:
			if (v != last) {
				last = v;
				last_t = now;
				seen = 1;
			} else if (seen && now - last_t >= US(gap)) {
				goto boundary;	/* the pause has started */
			}
			break;
		default:	/* LATCH */
			if (!v)
				seen = 1;	/* the clearing write has landed */
			else if (seen)
				goto boundary;
			break;
		}
		if (now - t0 > limit) {
			*us = timeout_us;
			return -1;
		}
	}
boundary:
	*us = (now - t0) / TICKS_PER_US;
	return 0;
}
