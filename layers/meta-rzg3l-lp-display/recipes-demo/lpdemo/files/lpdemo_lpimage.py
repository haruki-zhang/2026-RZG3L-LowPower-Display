# SPDX-License-Identifier: BSD-3-Clause
"""
RZ/G3L LowPower Display Demo: the two LP layers shown while the CM33 owns the display
(design_handoff_lpdemo_gui section 7).

  RPF0  720x88 at (600, 436)  "LOW POWER MODE" + how to return             (static)
  RPF1  720x88 at (600, 556)  power read-out: number, W, divider, saving   (template)

Both are RGB565 little endian, stride = width * 2, on a full-screen background (virtual RPF)
of LP_BG. The GUI (or lpdemo-ctl) renders them before the suspend; during the LP period the
CM33 rewrites the number and the percentage of RPF1 with the glyph tables that
lpdemo-cm33-fw/tools/gen_lp_glyphs.py generates from this module (same fonts, same
geometry: draw_run() below is the model the CM33 renderer follows).

Text is drawn with PangoCairo (letter spacing is a Pango attribute). Coordinates are those
of the handoff: x / y in px from the top left of the layer, y of text = baseline.
"""

import gi

gi.require_version("Pango", "1.0")
gi.require_version("PangoCairo", "1.0")
from gi.repository import Pango, PangoCairo  # noqa: E402
import cairo  # noqa: E402

LP_BG = 0x111126            # RGB565 0x1084
WHITE = 0xFFFFFF
SKY = 0x43ABE2
GREEN = 0x8CC56A
LP_MUTED = 0xA9A9C4
LP_DIVIDER = 0x2E2E52

LAYER_W = 720
LAYER_H = 88
RPF0_X, RPF0_Y = 600, 436
RPF1_X, RPF1_Y = 600, 556

SANS = "DejaVu Sans"
SANS_BOLD = "DejaVu Sans Bold"
COND_BOLD = "DejaVu Sans Condensed Bold"

MINUS = "−"            # all minus signs on the screens are U+2212

# RPF1 geometry (handoff 7.2)
POWER_PX = 108
POWER_SPACING = -3
POWER_BASELINE = 81
UNIT_PX = 44
UNIT_RIGHT = 412            # right end of the "W"
UNIT_GAP = 12               # number -> "W"
DIVIDER_X = 440
DIVIDER_Y0, DIVIDER_Y1 = 6, 82
PCT_PX = 52
PCT_X = 469
PCT_BASELINE = 52
NOTE_PX = 18
NOTE_BASELINE = 81
# Rows 84..87 of RPF1 carry no text: the CM33 draws its debug progress bar there
PROGRESS_Y = 84


def rgb(c):
    return ((c >> 16) & 0xFF) / 255.0, ((c >> 8) & 0xFF) / 255.0, (c & 0xFF) / 255.0


def layout(cr, text, font, px, spacing=0):
    """Pango layout of text, font at px pixels, letter spacing in px."""
    lay = PangoCairo.create_layout(cr)
    fd = Pango.FontDescription.from_string(font)
    fd.set_absolute_size(px * Pango.SCALE)
    lay.set_font_description(fd)
    if spacing:
        attrs = Pango.AttrList()
        attrs.insert(Pango.attr_letter_spacing_new(int(round(spacing * Pango.SCALE))))
        lay.set_attributes(attrs)
    lay.set_text(text, -1)
    return lay


def width(lay, spacing=0):
    """
    Logical width in px, CSS style: the letter spacing also follows the last character
    (Pango only puts it between characters).
    """
    return lay.get_extents()[1].width / Pango.SCALE + spacing


def show(cr, lay, x, baseline, colour):
    """Draws lay with its logical left edge at x and its baseline at baseline."""
    cr.set_source_rgb(*rgb(colour))
    cr.move_to(x, baseline - lay.get_baseline() / Pango.SCALE)
    PangoCairo.show_layout(cr, lay)


def text(cr, s, font, px, x, baseline, colour, spacing=0, align="left"):
    """Draws s; align left / centre / right relative to x. Returns the logical width."""
    lay = layout(cr, s, font, px, spacing)
    w = width(lay, spacing)
    if align == "centre":
        x -= w / 2.0
    elif align == "right":
        x -= w
    show(cr, lay, x, baseline, colour)
    return w


def advance(cr, ch, font, px):
    """Advance of one character in whole px (hinted metrics)."""
    return int(round(layout(cr, ch, font, px).get_extents()[1].width / Pango.SCALE))


def run_origins(cr, s, font, px, spacing, x, align="left"):
    """
    Pen positions of the characters of s drawn one by one, CSS letter spacing model:
    every character advances by its own advance + spacing. This is what the CM33 does
    with its glyph tables, so the template and the CM33 produce the same pixels.
    """
    advs = [advance(cr, ch, font, px) + spacing for ch in s]
    if align == "right":
        x -= sum(advs)
    out = []
    for a in advs:
        out.append(x)
        x += a
    return out


def draw_run(cr, s, font, px, spacing, x, baseline, colour, align="left"):
    for ch, ox in zip(s, run_origins(cr, s, font, px, spacing, x, align)):
        show(cr, layout(cr, ch, font, px), ox, baseline, colour)


def power_text(mw):
    """'d.ddd' (W) or '-.---' when there is no valid value."""
    if mw is None or mw < 0 or mw > 9999:
        return "-.---"
    return "%d.%03d" % (mw // 1000, mw % 1000)


def saving_pct(mw, baseline_mw):
    """round((1 - mw / baseline) * 100) limited to 0..99, None if unknown."""
    if mw is None or mw < 0 or not baseline_mw or baseline_mw <= 0:
        return None
    pct = (100 * (baseline_mw - mw) * 2 + baseline_mw) // (2 * baseline_mw)
    return max(0, min(99, pct))


def pct_text(pct):
    return "" if pct is None else "%s%d%%" % (MINUS, pct)


def unit_left(cr):
    """Logical left edge of the "W" of RPF1."""
    return UNIT_RIGHT - width(layout(cr, "W", SANS_BOLD, UNIT_PX))


def power_right(cr):
    """Right end of the number of RPF1 (CSS width: includes the trailing spacing)."""
    return unit_left(cr) - UNIT_GAP


def _surface():
    surf = cairo.ImageSurface(cairo.FORMAT_RGB16_565, LAYER_W, LAYER_H)
    assert surf.get_stride() == LAYER_W * 2
    cr = cairo.Context(surf)
    cr.set_source_rgb(*rgb(LP_BG))
    cr.paint()
    return surf, cr


def _rgb565(surf):
    """Native endian RGB565 = little endian on aarch64 (and x86)."""
    surf.flush()
    return bytes(surf.get_data())


def render_rpf0():
    """RPF0: the static layer, RGB565 little endian bytes."""
    surf, cr = _surface()
    text(cr, "LOW POWER MODE", COND_BOLD, 72, LAYER_W / 2.0, 55, WHITE, spacing=1,
         align="centre")
    text(cr, "Press USER_SW3 to return to normal mode", SANS_BOLD, 20, LAYER_W / 2.0, 81,
         SKY, align="centre")
    return _rgb565(surf)


def render_rpf1(power_mw=None, baseline_mw=None):
    """
    RPF1: the read-out template with the value at the hand-over, RGB565 little endian
    bytes. The CM33 redraws the number and the percentage from then on.
    """
    surf, cr = _surface()
    draw_rpf1(cr, power_mw, baseline_mw)
    return _rgb565(surf)


def draw_rpf1(cr, power_mw, baseline_mw):
    text(cr, "W", SANS_BOLD, UNIT_PX, UNIT_RIGHT, POWER_BASELINE, SKY, align="right")
    cr.set_source_rgb(*rgb(LP_DIVIDER))
    cr.rectangle(DIVIDER_X, DIVIDER_Y0, 1, DIVIDER_Y1 - DIVIDER_Y0)
    cr.fill()
    text(cr, "saved vs normal mode", SANS, NOTE_PX, PCT_X, NOTE_BASELINE, LP_MUTED)
    draw_run(cr, power_text(power_mw), SANS_BOLD, POWER_PX, POWER_SPACING, power_right(cr),
             POWER_BASELINE, WHITE, align="right")
    draw_run(cr, pct_text(saving_pct(power_mw, baseline_mw)), SANS_BOLD, PCT_PX, 0, PCT_X,
             PCT_BASELINE, GREEN)


def to_rgb888(data, w=LAYER_W, h=LAYER_H):
    """RGB565 little endian -> RGB888 bytes (previews, tests)."""
    out = bytearray(w * h * 3)
    for i in range(w * h):
        v = data[2 * i] | (data[2 * i + 1] << 8)
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        out[3 * i] = (r << 3) | (r >> 2)
        out[3 * i + 1] = (g << 2) | (g >> 4)
        out[3 * i + 2] = (b << 3) | (b >> 2)
    return bytes(out)
