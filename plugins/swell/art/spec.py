"""
Swell's panel in the range's Nazaré style. python3 spec.py writes spec.json next to it.
A row per strip: input, the three bands (level, frequency, IN and a drive lamp),
output, strip IN and a meter. Folded for stereo, open for dual or mid/side.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "shared", "indiewave", "art"))
from specbuild import Spec, sweep, polar, LABEL, SMALL, TINY

W = 1960
ROW1, ROW2 = 170, 420
S = Spec("swell", "Swell", lacquer="#f1dfa4")

SW = sweep(61)
S.kind("trim", "skirted", d=70, angles=SW, pad=22)
S.kind("level", "skirted", d=88, angles=SW)
S.kind("freq", "skirted", d=58, angles=SW, pad=20)
S.kind("sw", "toggle", s=0.68)
S.kind("drive", "jewel", colour="amber", s=0.7)
S.kind("mode", "chicken", s=0.6, angles=[-50, 0, 50])

BANDS = [("lo", 560, "LO SHELF", 20, 420, [(20, "20"), (140, "140"), (280, "280"), (420, "420")]),
         ("mid", 880, "MID PEAK", 200, 6200, [(200, ".2k"), (2200, "2.2k"), (4200, "4.2k"), (6200, "6.2k")]),
         ("hi", 1200, "HI SHELF", 5000, 25000, [(5000, "5k"), (10000, "10k"), (15000, "15k"), (20000, "20k"), (25000, "25k")])]
X_IN, X_OUT, X_ACTIVE, X_VU = 410, 1530, 1635, 1795


def at(v, lo, hi):
    return -135 + 270 * (v - lo) / (hi - lo)


def row(F, p, cy):
    for id, x, name in ((f"{p}_input", X_IN, "INPUT"), (f"{p}_output", X_OUT, "OUTPUT")):
        S.control(id, "trim", F.name, x, cy)
        F.dots(x, cy, 46, sweep(41), major=10)
        F.legends(x, cy, 60, [at(-20, -20, 20), 0, at(20, -20, 20)], ["−20", "0", "+20"])
        F.text(name, x, cy + 84, SMALL)
    for band, x, title, lo, hi, marks in BANDS:
        S.control(f"{p}_{band}_gain", "level", F.name, x, cy)
        F.dots(x, cy, 56, sweep(21), major=5, big=True)
        F.legends(x, cy, 70, [at(v, -10, 10) for v in (-10, 0, 10)], ["\u221210", "0", "+10"])
        F.text("LEVEL", x, cy + 84, SMALL)
        S.control(f"{p}_{band}_freq", "freq", F.name, x + 120, cy + 6)
        F.dots(x + 120, cy + 6, 40, sweep(21), major=5)
        F.legends(x + 120, cy + 6, 53, [at(v, lo, hi) for v, _ in marks], [t for _, t in marks])
        F.text("FREQ", x + 120, cy + 84, SMALL)
        S.control(f"{p}_{band}_in", "sw", F.name, x + 205, cy + 14)
        F.text("IN", x + 205, cy + 84, SMALL)
        S.control(f"drive_{p}_{band}", "drive", F.name, x + 205, cy - 44)
        F.text("DRIVE", x + 205, cy - 70, TINY)
    S.control(f"{p}_in", "sw", F.name, X_ACTIVE, cy + 6)
    F.text("STRIP", X_ACTIVE, cy - 44, TINY)
    F.text("IN", X_ACTIVE, cy + 84, SMALL)
    F.vu(f"vu_{p}", X_VU, cy, 230, 146)


def common(F):
    F.pinstripes()
    F.badge(200, 118, 196, 100, "Swell")
    F.text("HARMONIC EQUALISER", 200, 188, dict(size=7.5, ls=2.4))
    S.control("mode", "mode", F.name, 200, 262)
    for a in (-50, 0, 50):
        x, y = polar(200, 262, 34, a)
        F.dot(x, y, 1.6)
    F.legends(200, 262, 47, [-50, 0, 50], ["DUAL", "STEREO", "M·S"])
    for band, x, title, *_ in BANDS:
        x0, x1 = x - 70, x + 235
        cx = (x0 + x1) / 2
        F.text(title, cx, 52, LABEL)
        half = len(title) * 6 + 16
        F.line(x0, 52, cx - half, 52, 0.8)
        F.line(cx + half, 52, x1, 52, 0.8)


folded = S.face("folded", (W, 320))
common(folded)
row(folded, "a", ROW1)

opened = S.face("open", (W, 570))
common(opened)
row(opened, "a", ROW1)
row(opened, "b", ROW2)
opened.text("B", 200, ROW2 - 8, dict(size=30, ls=0))
opened.text("SECOND STRIP", 200, ROW2 + 30, TINY)
opened.line(110, ROW2 - 92, W - 110, ROW2 - 92, 0.6, alpha=0.6)

S.write(os.path.dirname(os.path.abspath(__file__)))
