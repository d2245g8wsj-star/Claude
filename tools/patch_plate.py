#!/usr/bin/env python3
"""Paints two pieces of printing out of the plate render.

The plate is a baked 2800x940 Blender render, so anything printed on it is
fixed. Two printed things have to go:

  1. The subtitle "CONVERTER . CLIPPER" at the top right, which is replaced by
     a longer line drawn in code (see drawTrackedLabel in PluginEditor.cpp).
  2. The transfer inset and its TRANSFER legend at the right hand end of the
     display window. The waveform writes its newest column at the right edge
     and scrolls left, so an inset there covers material exactly as it
     arrives. The inset moves to the lower left, drawn in code.

Both are repaired by copying a clean piece of the same surface from the same
rows, so the vertical brightness gradient and the horizontal grain both stay
continuous, and blending it in behind an alpha feather so there is no seam.

Run from the repository root:

    python3 tools/patch_plate.py

It is idempotent in effect but not in principle: it reads and rewrites
resources/gui/plate.png in place, so run it once per fresh plate render. When
the Blender side bakes the new subtitle and moves the inset, delete this file
along with the code that draws them (see docs/ART-BRIEF-PLATE-V5.md).
"""

import sys
from PIL import Image

PLATE = "resources/gui/plate.png"


def ramp(distance, width):
    """Alpha for a pixel `distance` px inside a feathered edge of `width`."""
    if width <= 0:
        return 1.0
    return min(1.0, max(0.0, distance / width))


def patch(px, dest, shift_x, feather):
    """Copies dest shifted by shift_x onto dest, feathered on the named edges.

    dest is (x0, y0, x1, y1), exclusive of x1/y1. feather maps an edge name to
    a ramp width in pixels; an edge left out is a hard cut, which is only safe
    where the two sides are known to be the same colour.
    """
    x0, y0, x1, y1 = dest

    # Read the whole source block first: the copy overlaps itself in x when the
    # shift is smaller than the width, and reading as we write would smear it.
    source = [[px[x + shift_x, y] for x in range(x0, x1)] for y in range(y0, y1)]

    for j, y in enumerate(range(y0, y1)):
        for i, x in enumerate(range(x0, x1)):
            a = 1.0

            if "left" in feather:
                a = min(a, ramp(i, feather["left"]))
            if "right" in feather:
                a = min(a, ramp(x1 - 1 - x, feather["right"]))
            if "top" in feather:
                a = min(a, ramp(j, feather["top"]))
            if "bottom" in feather:
                a = min(a, ramp(y1 - 1 - y, feather["bottom"]))

            if a <= 0.0:
                continue

            sr, sg, sb = source[j][i]

            if a >= 1.0:
                px[x, y] = (sr, sg, sb)
            else:
                dr, dg, db = px[x, y]
                px[x, y] = (round(dr + (sr - dr) * a),
                            round(dg + (sg - dg) * a),
                            round(db + (sb - db) * a))


def main():
    image = Image.open(PLATE).convert("RGB")

    if image.size != (2800, 940):
        sys.exit(f"expected a 2800x940 plate, found {image.size[0]}x{image.size[1]}")

    px = image.load()

    # ---- the subtitle -----------------------------------------------------
    # Printed ink measured at px (2268, 62)-(2531, 73). The wood at the same
    # rows from x 1560 to 2260 is empty, so a 400 px shift lands on clean
    # surface without crossing the CREST wordmark or the corner screw.
    patch(px, (2256, 40, 2548, 96), shift_x=-400,
          feather={"left": 12, "right": 12, "top": 10, "bottom": 10})

    # ---- the transfer inset ----------------------------------------------
    # The legend sits at y 308-331 and the well at y 334-586, from x 1694 to
    # the window's right edge at 1960. The window's grid pitch is 81.3 px, so
    # a shift of six cells (488 px) puts every copied vertical line back on
    # the pitch: the source lines at 1228/1310/1391/1472 land on
    # 1716/1798/1879/1960, which is where the printed ones already are.
    #
    # Horizontal lines and the emphasised centre line come along untouched
    # because the copy stays on its own rows. The right and bottom edges are
    # hard cuts: both meet the window's own dark border, and the field either
    # side of them is the same flat colour.
    patch(px, (1676, 296, 1961, 600), shift_x=-488,
          feather={"left": 14, "top": 12})

    image.save(PLATE)
    print(f"patched {PLATE}")


if __name__ == "__main__":
    main()
