# SPDX-License-Identifier: BSD-3-Clause
"""
RZ/G3L LowPower Display Demo: the Normal screen of lpdemo-gui (design_handoff_lpdemo_gui
section 6), drawing and power history. No GTK here: lpdemo-gui places the pieces with a
Gtk.Fixed, previews and tests draw them on a cairo image surface.

All coordinates are screen coordinates of the 1920x1080 output, text y = baseline unless
called top (top of the logical box, the CSS "box top" of the handoff).
"""

import bisect

import lpdemo_lpimage as T
from lpdemo_lpimage import Pango, PangoCairo

SCREEN_W, SCREEN_H = 1920, 1080

PAPER = 0xF2F2F2
BLUE = 0x2A289D
BLUE_PRESSED = 0x201E7E
BUTTON_DISABLED = 0x9897CC
WHITE = 0xFFFFFF
GREY = 0x706F6F
HAIRLINE = 0xD9D9D9
LP_BAND = 0xDCEEF9

MINUS = T.MINUS

# 6.1 logo
LOGO_X, LOGO_Y, LOGO_W, LOGO_H = 72, 68, 240, 40
LOGO_FILE = "renesas-wordmark-240x40.png"

# 6.2 current power; POWER_AREA = the widget lpdemo-gui redraws (x, y, w, h)
POWER_AREA = (72, 190, 1100, 360)
VALUE_X, VALUE_BASELINE = 72, 494
VALUE_PX, VALUE_SPACING = 300, -8
UNIT_PX, UNIT_GAP = 120, 28

# 6.3 button
BUTTON = (1288, 270, 560, 200)
BUTTON_LABEL = "Enter Low Power"
BUTTON_PX = 52

# 6.4 chart panel
CHART = (72, 628, 1776, 380)
CHART_BORDER = 4
PLOT_X0, PLOT_X1 = 184, 1808
PLOT_Y0, PLOT_Y1 = 704, 952
Y_MAX_MW = 2500
# History shown: 10 min (handoff: 30 min, changed after the first board test)
WINDOW_S = 600
CHART_TITLE = "LAST 10 MIN"
# X axis labels: (seconds before now, text, alignment)
X_TICKS = ((600, MINUS + "10 min", "left"), (300, MINUS + "5", "centre"), (0, "now", "right"))
LINE_W = 4
SAMPLE_S = 2.0              # GUI sampling period, = CM33 LP ring period
GAP_S = 10.0                # no line across a gap without samples longer than this
BASELINE_S = 10.0           # baseline of the saving: mean of the last 10 s


def fill(cr, colour, x, y, w, h):
    cr.set_source_rgb(*T.rgb(colour))
    cr.rectangle(x, y, w, h)
    cr.fill()


def text_top(cr, s, font, px, x, top, colour, spacing=0, align="left"):
    """Text with the top of its logical box at top (CSS box top). Returns the width."""
    lay = T.layout(cr, s, font, px, spacing)
    w = T.width(lay, spacing)
    if align == "centre":
        x -= w / 2.0
    elif align == "right":
        x -= w
    cr.set_source_rgb(*T.rgb(colour))
    cr.move_to(x, top)
    PangoCairo.show_layout(cr, lay)
    return w


def run_width(cr, s, font, px, spacing):
    return sum(T.advance(cr, ch, font, px) + spacing for ch in s)


def draw_power(cr, mw):
    """6.2: "CURRENT POWER", the value (power_avg_mw, None = invalid) and "W"."""
    text_top(cr, "CURRENT POWER", T.COND_BOLD, 28, 72, 200, GREY, spacing=3)
    s = T.power_text(mw)
    T.draw_run(cr, s, T.SANS_BOLD, VALUE_PX, VALUE_SPACING, VALUE_X, VALUE_BASELINE, BLUE)
    x = VALUE_X + run_width(cr, s, T.SANS_BOLD, VALUE_PX, VALUE_SPACING) + UNIT_GAP
    T.text(cr, "W", T.SANS_BOLD, UNIT_PX, x, VALUE_BASELINE, BLUE)


def draw_button(cr, colour=BLUE):
    """6.3 as GTK draws it with the lpdemo-gui CSS (previews only)."""
    x, y, w, h = BUTTON
    fill(cr, colour, x, y, w, h)
    lay = T.layout(cr, BUTTON_LABEL, T.SANS_BOLD, BUTTON_PX)
    log = lay.get_extents()[1]
    cr.set_source_rgb(*T.rgb(WHITE))
    cr.move_to(x + (w - log.width / Pango.SCALE) / 2.0, y + (h - log.height / Pango.SCALE) / 2.0)
    PangoCairo.show_layout(cr, lay)


def x_of(t, now):
    return PLOT_X0 + (t - (now - WINDOW_S)) / WINDOW_S * (PLOT_X1 - PLOT_X0)


def y_of(mw):
    return PLOT_Y1 - mw / float(Y_MAX_MW) * (PLOT_Y1 - PLOT_Y0)


def draw_chart(cr, now, points, bands):
    """
    6.4: the panel with the last WINDOW_S seconds.
    points: [(t, mW or None)] sorted by t; bands: [(t0, t1)] low power periods
    (t = CLOCK_BOOTTIME seconds, now = the right end).
    """
    x, y, w, h = CHART
    fill(cr, WHITE, x, y, w, h)
    fill(cr, BLUE, x, y, w, CHART_BORDER)

    text_top(cr, CHART_TITLE, T.COND_BOLD, 24, 112, 660, GREY, spacing=2)
    lw = text_top(cr, "Low power", T.SANS, 20, PLOT_X1, 662, GREY, align="right")
    lay = T.layout(cr, "Low power", T.SANS, 20)
    box_h = lay.get_extents()[1].height / Pango.SCALE
    fill(cr, LP_BAND, PLOT_X1 - lw - 10 - 20, 662 + (box_h - 20) / 2.0, 20, 20)

    # Low power bands first, then the grid and the curve
    cr.save()
    cr.rectangle(PLOT_X0, PLOT_Y0, PLOT_X1 - PLOT_X0, PLOT_Y1 - PLOT_Y0)
    cr.clip()
    for t0, t1 in bands:
        x0, x1 = max(PLOT_X0, x_of(t0, now)), min(PLOT_X1, x_of(t1, now))
        if x1 > x0:
            fill(cr, LP_BAND, x0, PLOT_Y0, x1 - x0, PLOT_Y1 - PLOT_Y0)
    cr.restore()

    fill(cr, HAIRLINE, PLOT_X0, PLOT_Y0, PLOT_X1 - PLOT_X0, 1)                  # 2.5 W
    fill(cr, HAIRLINE, PLOT_X0, y_of(1000), PLOT_X1 - PLOT_X0, 1)               # 1.0 W
    for label, top in (("2.5 W", 696), ("1.0 W", 842), ("0", 941)):
        text_top(cr, label, T.SANS, 18, 112, top, GREY)
    for before, label, align in X_TICKS:
        text_top(cr, label, T.SANS, 18, x_of(now - before, now), 963, GREY, align=align)

    # Curve: clipped to the plot (vertically with room for the line width)
    cr.save()
    cr.rectangle(PLOT_X0, PLOT_Y0 - LINE_W, PLOT_X1 - PLOT_X0, PLOT_Y1 - PLOT_Y0 + 2 * LINE_W)
    cr.clip()
    cr.set_source_rgb(*T.rgb(BLUE))
    cr.set_line_width(LINE_W)
    cr.set_line_join(1)         # cairo.LINE_JOIN_ROUND
    cr.set_line_cap(1)          # cairo.LINE_CAP_ROUND
    start = max(0, bisect.bisect_left([p[0] for p in points], now - WINDOW_S) - 1)
    prev = None
    drawn = False
    for t, mw in points[start:]:
        if mw is None:
            prev = None
            continue
        px, py = x_of(t, now), y_of(mw)
        if prev is None or t - prev > GAP_S:
            cr.move_to(px, py)
        else:
            cr.line_to(px, py)
            drawn = True
        prev = t
    if drawn:
        cr.stroke()
    cr.new_path()
    cr.restore()
    # The 0 W axis over the curve
    fill(cr, GREY, PLOT_X0, PLOT_Y1 - 1, PLOT_X1 - PLOT_X0, 2)


class History:
    """Power samples of the last WINDOW_S seconds and the low power periods."""

    def __init__(self):
        self.points = []        # [(t, mW or None)], sorted by t
        self.bands = []         # [(t0, t1)]
        self._lp_start = None

    def add(self, t, mw):
        if self.points and t < self.points[-1][0]:
            bisect.insort(self.points, (t, mw), key=lambda p: p[0])
        else:
            self.points.append((t, mw))

    @property
    def lp_active(self):
        return self._lp_start is not None

    def lp_begin(self, t):
        self._lp_start = t

    def lp_cancel(self):
        self._lp_start = None

    def lp_end(self, t, samples=()):
        """samples: [(t, mW or None)] the CM33 took during the LP period."""
        if self._lp_start is None:
            return
        t0, self._lp_start = self._lp_start, None
        self.bands.append((t0, t))
        for ts, mw in samples:
            if t0 - SAMPLE_S <= ts <= t + SAMPLE_S:
                self.add(ts, mw)

    def baseline_mw(self, now):
        """Mean of the valid samples of the last BASELINE_S seconds, or None."""
        v = [mw for t, mw in self.points if t > now - BASELINE_S and mw is not None]
        return int(round(sum(v) / float(len(v)))) if v else None

    def prune(self, now):
        limit = now - WINDOW_S - GAP_S
        i = bisect.bisect_left([p[0] for p in self.points], limit)
        if i > 1:
            del self.points[:i - 1]     # keep one before the window for the line
        self.bands = [b for b in self.bands if b[1] >= limit]
