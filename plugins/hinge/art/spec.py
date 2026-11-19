"""
Hinge's panel in the range's Nazaré style. python3 spec.py writes spec.json next to it.
Two faces: folded (one row, stereo) and open (a second row for dual or mid/side).
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "shared", "indiewave", "art"))
from specbuild import Spec, sweep, polar, LABEL, SMALL, TINY

W = 2250
ROW1, ROW2 = 165, 415
S = Spec("hinge", "Hinge", lacquer="#cbdab7")

SEL8 = sweep(8, -120, 120)
STEP21 = sweep(21)
SWEEP = sweep(61)
S.kind("sel", "skirted", d=64, angles=SEL8, pad=22)
S.kind("level", "skirted", d=92, angles=STEP21)
S.kind("sweep", "skirted", d=64, angles=SWEEP, pad=22)
S.kind("out", "skirted", d=80, angles=STEP21)
S.kind("engage", "toggle", s=0.72)
S.kind("lamp", "jewel", colour="amber", s=0.8)
S.kind("mode", "chicken", s=0.6, angles=[-50, 0, 50])

X = dict(lo_cut=420, lo_shelf=570, lo_level=730, hi_level=900, hi_shelf=1060, hi_cut=1210,
         dyn_threshold=1370, lo_dyn=1500, hi_dyn=1630, output=1775, engage=1875, vu=2060)

LO_CUT = ["OUT", "12", "18", "24", "30", "36", "43", "54"]
LO_SHELF = ["74", "84", "98", "116", "131", "166", "230", "361"]
HI_SHELF = ["18k", "7k1", "4k8", "3k4", "2k5", "2k1", "1k8", "1k6"]
HI_CUT = ["OUT", "70k", "28k", "18k", "12k6", "11k1", "9k", "7k5"]


def at(v, lo, hi):
    return -135 + 270 * (v - lo) / (hi - lo)


def row(F, prefix, cy):
    def sel(id, texts, name):
        S.control(f"{prefix}_{id}", "sel", F.name, X[id], cy)
        F.dots(X[id], cy, 42, SEL8)
        F.legends(X[id], cy, 55, SEL8, texts)
        F.text(name, X[id], cy + 82, SMALL)

    def level(id, name):
        S.control(f"{prefix}_{id}", "level", F.name, X[id], cy)
        F.dots(X[id], cy, 58, STEP21, major=5, big=True)
        F.legends(X[id], cy, 72, [STEP21[i] for i in (0, 5, 10, 15, 20)], ["5", "2.5", "0", "2.5", "5"])
        F.text(name, X[id], cy + 82, SMALL)
        F.text("−", X[id] - 46, cy + 82, dict(size=11))
        F.text("+", X[id] + 46, cy + 82, dict(size=11))

    def swept(id, name, values, lo, hi):
        S.control(f"{prefix}_{id}", "sweep", F.name, X[id], cy)
        F.dots(X[id], cy, 42, sweep(21), major=5)
        F.legends(X[id], cy, 55, [at(v, lo, hi) for v, _ in values], [t for _, t in values])
        F.text(name, X[id], cy + 82, SMALL)

    sel("lo_cut", LO_CUT, "CUT HZ")
    sel("lo_shelf", LO_SHELF, "SHELF HZ")
    level("lo_level", "LEVEL")
    level("hi_level", "LEVEL")
    sel("hi_shelf", HI_SHELF, "SHELF HZ")
    sel("hi_cut", HI_CUT, "CUT HZ")
    swept("dyn_threshold", "THRESHOLD", [(-48, "−48"), (-36, "36"), (-24, "24"), (-12, "12"), (0, "0")], -48, 0)
    swept("lo_dyn", "LOW", [(-5, "−5"), (0, "0"), (5, "+5")], -5, 5)
    swept("hi_dyn", "HIGH", [(-5, "−5"), (0, "0"), (5, "+5")], -5, 5)

    S.control(f"{prefix}_output", "out", F.name, X["output"], cy)
    F.dots(X["output"], cy, 52, STEP21, major=5)
    F.legends(X["output"], cy, 65, [STEP21[0], STEP21[10], STEP21[20]], ["−", "0", "+"], dict(size=10))
    F.text("OUTPUT", X["output"], cy + 82, SMALL)

    S.control(f"{prefix}_engage", "engage", F.name, X["engage"], cy + 18)
    S.control(f"lamp_{prefix}", "lamp", F.name, X["engage"], cy - 44)
    F.text("IN", X["engage"] - 30, cy + 2, TINY)
    F.text("ENGAGE", X["engage"], cy + 82, SMALL)

    F.vu(f"vu_{prefix}", X["vu"], cy, 230, 146)


def common(F):
    F.pinstripes()
    F.badge(200, 118, 196, 100, "Hinge")
    F.text("SHELVING EQUALISER", 200, 188, dict(size=7.5, ls=2.4))
    S.control("mode", "mode", F.name, 200, 262)
    for a in (-50, 0, 50):
        x, y = polar(200, 262, 34, a)
        F.dot(x, y, 1.6)
    F.legends(200, 262, 47, [-50, 0, 50], ["STEREO", "DUAL", "M·S"])
    # section titles over the first row
    for title, a, b in (("LOW", "lo_cut", "lo_level"), ("HIGH", "hi_level", "hi_cut"),
                        ("DYNAMICS", "dyn_threshold", "hi_dyn"), ("OUT", "output", "engage")):
        x0, x1 = X[a] - 50, X[b] + 50
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
opened.text("II", 200, ROW2 - 8, dict(size=30, ls=0))
opened.text("SECOND CHANNEL", 200, ROW2 + 30, TINY)
opened.line(110, ROW2 - 92, W - 110, ROW2 - 92, 0.6, alpha=0.6)

S.write(os.path.dirname(os.path.abspath(__file__)))
