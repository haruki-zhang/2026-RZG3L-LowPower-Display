#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""
Generates src/lp_glyphs.h: the glyphs and the geometry the CM33 uses to redraw the power
read-out of the LP picture (RPF1, design_handoff_lpdemo_gui section 7.2).

    tools/gen_lp_glyphs.py > src/lp_glyphs.h

The geometry and the text drawing come from lpdemo_lpimage.py (recipes-demo/lpdemo), the
module that renders the RPF1 template on the target, so both draw the same pixels:
characters are drawn one by one at whole pixel pen positions, advance + letter spacing.

Needs Python 3 with PyGObject (Pango, PangoCairo), pycairo and the DejaVu 2.37 fonts of the
image (ttf-dejavu: DejaVuSans.ttf, DejaVuSans-Bold.ttf, DejaVuSansCondensed-Bold.ttf),
e.g. FONTCONFIG_FILE pointing at a fonts.conf with only these fonts. It is not run by the
firmware build (that would need pango-native etc.): the output is kept in the source tree.

Glyph format, made for 32-bit stores of two RGB565 pixels: 4-bit coverage, two pixels per
byte (low nibble = left pixel), in a box that starts on an even screen x. As the parity of
the pen position is only known at run time, every glyph comes in two variants:
  variant k = (pen + x[0]) & 1, box left = pen + x[k] (even), width w[k] (even)
The CM33 maps each byte through a 256-entry table to one 32-bit word (two pixels).

Checked here, relied upon by the CM33 (readout.c):
  - the boxes of a run never overlap and stay inside its clear rectangle, which has even
    x bounds and no static ink of the template;
  - "d.ddd" (tabular digits): the box of every glyph that can appear at position i lies in
    the cell [LPG_POWER_CELL[i], LPG_POWER_CELL[i + 1]), so a changed digit is redrawn alone.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LAYER = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(LAYER, "recipes-demo", "lpdemo", "files"))

import cairo  # noqa: E402
import lpdemo_lpimage as L  # noqa: E402

POWER_CHARS = "0123456789.-"
PCT_CHARS = "0123456789" + L.MINUS + "%"
DIGITS = "0123456789"


class Glyph:
    def __init__(self, ch, font, px, cr):
        size = px * 2
        ox, base = px // 2, px + px // 2
        surf = cairo.ImageSurface(cairo.FORMAT_A8, size, size)
        c = cairo.Context(surf)
        L.show(c, L.layout(c, ch, font, px), ox, base, L.WHITE)
        surf.flush()
        data, stride = surf.get_data(), surf.get_stride()
        a4 = [[(data[y * stride + x] * 15 + 127) // 255 for x in range(size)]
              for y in range(size)]
        xs = [x for x in range(size) for y in range(size) if a4[y][x]]
        ys = [y for y in range(size) for x in range(size) if a4[y][x]]
        if not xs:
            raise SystemExit("glyph %r: no ink" % ch)
        x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
        if x0 < 2 or y0 == 0 or x1 > size - 2 or y1 == size:
            raise SystemExit("glyph %r clipped" % ch)
        self.ch = ch
        self.adv = L.advance(cr, ch, font, px)
        self.x = x0 - ox                # ink left, relative to the pen
        self.y = y0 - base              # ink top, relative to the baseline
        self.h = y1 - y0
        self.variants = []              # (box x rel. to the pen, even width, rows)
        for k in (0, 1):
            bx0 = x0 - k
            w = x1 - bx0
            w += w & 1
            self.variants.append((bx0 - ox, w, [a4[y][bx0:bx0 + w] for y in range(y0, y1)]))

    def box(self, pen, baseline):
        """Screen box (x0, y0, x1, y1) of the glyph drawn at pen."""
        bx, w, _ = self.variants[(pen + self.x) & 1]
        return (pen + bx, baseline + self.y, pen + bx + w, baseline + self.y + self.h)


def pack(rows):
    out = bytearray()
    for r in rows:
        for i in range(0, len(r), 2):
            out.append(r[i] | (r[i + 1] << 4))
    return bytes(out)


def union(a, b):
    if a is None:
        return b
    return (min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]))


def overlap(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def inside(a, r):
    return r[0] <= a[0] and r[1] <= a[1] and a[2] <= r[2] and a[3] <= r[3]


def run_boxes(cr, glyphs, s, font, px, spacing, x, baseline, align, name):
    """Boxes of the run s, drawn like lpdemo_lpimage.draw_run(); checks they do not overlap."""
    boxes = []
    for ch, pen in zip(s, L.run_origins(cr, s, font, px, spacing, x, align)):
        if pen != int(pen):
            raise SystemExit("%s %r: pen %r is not a whole pixel" % (name, s, pen))
        b = glyphs[ch].box(int(pen), baseline)
        for o in boxes:
            if overlap(o, b):
                raise SystemExit("%s %r: glyph boxes overlap" % (name, s))
        boxes.append(b)
    return boxes


def static_ink():
    """Ink mask of the template without the two runs (W, divider, note)."""
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, L.LAYER_W, L.LAYER_H)
    cr = cairo.Context(surf)
    cr.set_source_rgb(*L.rgb(L.LP_BG))
    cr.paint()
    L.text(cr, "W", L.SANS_BOLD, L.UNIT_PX, L.UNIT_RIGHT, L.POWER_BASELINE, L.SKY,
           align="right")
    cr.set_source_rgb(*L.rgb(L.LP_DIVIDER))
    cr.rectangle(L.DIVIDER_X, L.DIVIDER_Y0, 1, L.DIVIDER_Y1 - L.DIVIDER_Y0)
    cr.fill()
    L.text(cr, "saved vs normal mode", L.SANS, L.NOTE_PX, L.PCT_X, L.NOTE_BASELINE,
           L.LP_MUTED)
    surf.flush()
    d, st = surf.get_data(), surf.get_stride()
    bg = bytes(d[0:4])
    return {(x, y) for y in range(L.LAYER_H) for x in range(L.LAYER_W)
            if bytes(d[y * st + 4 * x:y * st + 4 * x + 4]) != bg}


def check_rect(r, ink, name):
    if r[0] & 1 or r[2] & 1:
        raise SystemExit("%s: clear rectangle x not even" % name)
    if r[0] < 0 or r[1] < 0 or r[2] > L.LAYER_W or r[3] > L.PROGRESS_Y:
        raise SystemExit("%s: clear rectangle outside the read-out rows" % name)
    if any(r[0] <= x < r[2] and r[1] <= y < r[3] for x, y in ink):
        raise SystemExit("%s: clear rectangle overlaps the static ink of the template" % name)


def c_name(ch):
    return {".": "dot", "-": "hyphen", L.MINUS: "minus", "%": "percent"}.get(ch, ch)


def emit_font(prefix, chars, glyphs, out):
    data = bytearray()
    entries = []
    for ch in chars:
        g = glyphs[ch]
        offs = []
        for _, _, rows in g.variants:
            offs.append(len(data))
            data += pack(rows)
        entries.append((g, offs))
    out.append("static const uint8_t %s_data[%d] = {" % (prefix, len(data)))
    for i in range(0, len(data), 16):
        out.append("\t" + " ".join("0x%02x," % b for b in data[i:i + 16]))
    out.append("};")
    out.append("")
    out.append("static const struct lpg_glyph %s_glyphs[%d] = {" % (prefix, len(chars)))
    for g, offs in entries:
        (x0, w0, _), (x1, w1, _) = g.variants
        out.append("\t{ 0x%04x, %d, %d, %d, { %d, %d }, { %d, %d }, { %d, %d } },\t/* %s */" %
                   (ord(g.ch), g.adv, g.y, g.h, x0, x1, w0, w1, offs[0], offs[1],
                    c_name(g.ch)))
    out.append("};")
    out.append("")
    return len(data)


def main():
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, 8, 8)
    cr = cairo.Context(surf)
    pw = {ch: Glyph(ch, L.SANS_BOLD, L.POWER_PX, cr) for ch in POWER_CHARS}
    pc = {ch: Glyph(ch, L.SANS_BOLD, L.PCT_PX, cr) for ch in PCT_CHARS}
    if len({pw[c].adv for c in DIGITS}) != 1 or len({pc[c].adv for c in DIGITS}) != 1:
        raise SystemExit("digits are not tabular in this font")
    ink = static_ink()

    # Power: "d.ddd" (cells fixed) and "-.---"
    right = L.power_right(cr)
    if right != int(right):
        raise SystemExit("power run right end %r is not a whole pixel" % right)
    right = int(right)
    args = (L.SANS_BOLD, L.POWER_PX, L.POWER_SPACING, right, L.POWER_BASELINE, "right")
    pens = [int(p) for p in L.run_origins(cr, "0.000", *args[:4], align="right")]
    cells = [p & ~1 for p in pens] + [right & ~1]
    prect = None
    for s in ["-.---"] + [d + "." + d * 3 for d in DIGITS]:
        for b in run_boxes(cr, pw, s, *args, name="power"):
            prect = union(prect, b)
    for i, pen in enumerate(pens):
        for ch in (DIGITS if i != 1 else "."):
            b = pw[ch].box(pen, L.POWER_BASELINE)
            if not (cells[i] <= b[0] and b[2] <= cells[i + 1]):
                raise SystemExit("power: %r does not fit in cell %d" % (ch, i))
    prect = (prect[0] & ~1, prect[1], (prect[2] + 1) & ~1, prect[3])
    prect = (min(prect[0], cells[0]), prect[1], max(prect[2], cells[-1]), prect[3])
    check_rect(prect, ink, "power")

    # Saving: "-d%" / "-dd%", left aligned
    qrect = None
    for d in DIGITS:
        for s in (d, d * 2):
            for b in run_boxes(cr, pc, L.MINUS + s + "%", L.SANS_BOLD, L.PCT_PX, 0, L.PCT_X,
                               L.PCT_BASELINE, "left", name="percentage"):
                qrect = union(qrect, b)
    qrect = (qrect[0] & ~1, qrect[1], (qrect[2] + 1) & ~1, qrect[3])
    check_rect(qrect, ink, "percentage")
    if overlap(prect, qrect):
        raise SystemExit("the two runs overlap")

    out = []
    out.append("/* SPDX-License-Identifier: BSD-3-Clause */")
    out.append("/*")
    out.append(" * GENERATED by tools/gen_lp_glyphs.py from recipes-demo/lpdemo/files/lpdemo_lpimage.py")
    out.append(" * (DejaVu Sans Bold %d px / %d px, 4-bit coverage). Do not edit." %
               (L.POWER_PX, L.PCT_PX))
    out.append(" */")
    out.append("#ifndef LP_GLYPHS_H")
    out.append("#define LP_GLYPHS_H")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("/*")
    out.append(" * Variant k = (pen + x[0]) & 1: box left = pen + x[k] (even screen x), w[k] (even)")
    out.append(" * columns, h rows of w[k] / 2 bytes from off[k], 2 pixels per byte, low nibble left.")
    out.append(" */")
    out.append("struct lpg_glyph {")
    out.append("\tuint16_t ch;\t\t/* character (Unicode) */")
    out.append("\tuint16_t adv;\t\t/* advance (px), without letter spacing */")
    out.append("\tint16_t y;\t\t/* box top, relative to the baseline */")
    out.append("\tuint16_t h;")
    out.append("\tint16_t x[2];")
    out.append("\tuint16_t w[2];")
    out.append("\tuint32_t off[2];")
    out.append("};")
    out.append("")
    out.append("/* RPF1 layer the template is rendered for */")
    out.append("#define LPG_LAYER_W\t\t%d" % L.LAYER_W)
    out.append("#define LPG_LAYER_H\t\t%d" % L.LAYER_H)
    out.append("#define LPG_PROGRESS_Y\t\t%d\t/* rows below: debug progress bar */" %
               L.PROGRESS_Y)
    out.append("")
    out.append("/* Power, \"d.ddd\": right aligned, letter spacing after every character */")
    out.append("#define LPG_POWER_RIGHT\t\t%d" % right)
    out.append("#define LPG_POWER_SPACING\t(%d)" % L.POWER_SPACING)
    out.append("#define LPG_POWER_BASELINE\t%d" % L.POWER_BASELINE)
    out.append("#define LPG_POWER_RGB\t\t0x%06Xu" % L.WHITE)
    out.append("#define LPG_POWER_CLEAR_X0\t%d" % prect[0])
    out.append("#define LPG_POWER_CLEAR_Y0\t%d" % prect[1])
    out.append("#define LPG_POWER_CLEAR_X1\t%d" % prect[2])
    out.append("#define LPG_POWER_CLEAR_Y1\t%d" % prect[3])
    out.append("/* \"d.ddd\": pen of each character, cell i = [CELL[i], CELL[i + 1]) (even) */")
    out.append("#define LPG_POWER_CHARS\t\t5")
    out.append("static const int16_t lpg_power_pen[5] = { %s };" % ", ".join(map(str, pens)))
    out.append("static const int16_t lpg_power_cell[6] = { %s };" % ", ".join(map(str, cells)))
    out.append("")
    out.append("/* Saving, \"\\u2212dd%%\": left aligned */")
    out.append("#define LPG_PCT_LEFT\t\t%d" % L.PCT_X)
    out.append("#define LPG_PCT_SPACING\t\t0")
    out.append("#define LPG_PCT_BASELINE\t%d" % L.PCT_BASELINE)
    out.append("#define LPG_PCT_RGB\t\t0x%06Xu" % L.GREEN)
    out.append("#define LPG_PCT_CLEAR_X0\t%d" % qrect[0])
    out.append("#define LPG_PCT_CLEAR_Y0\t%d" % qrect[1])
    out.append("#define LPG_PCT_CLEAR_X1\t%d" % qrect[2])
    out.append("#define LPG_PCT_CLEAR_Y1\t%d" % qrect[3])
    out.append("")
    out.append("#define LPG_CH_MINUS\t\t0x2212u")
    out.append("")
    n1 = emit_font("lpg_power", POWER_CHARS, pw, out)
    n2 = emit_font("lpg_pct", PCT_CHARS, pc, out)
    out.append("#define LPG_POWER_GLYPHS\t%d" % len(POWER_CHARS))
    out.append("#define LPG_PCT_GLYPHS\t\t%d" % len(PCT_CHARS))
    out.append("/* glyph data: %d + %d bytes */" % (n1, n2))
    out.append("")
    out.append("#endif /* LP_GLYPHS_H */")
    print("\n".join(out))


if __name__ == "__main__":
    main()
