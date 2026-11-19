"""
Beacon's panel in the range's Nazaré style. python3 spec.py writes spec.json next to it.
One row of controls for stereo; mid/side and a split dual mono fold out a second
row for the side (or right) channel, on the side_* parameters.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "shared", "indiewave", "art"))
from specbuild import Spec, sweep, polar, LABEL, SMALL, TINY

W = 2150
ROW1, ROW2 = 125, 385
S = Spec("beacon", "Beacon", lacquer="#f3c6a0")

SW = sweep(61)
S.kind("big", "skirted", d=96, angles=SW)
S.kind("trim", "skirted", d=78, angles=SW)
S.kind("small", "skirted", d=60, angles=SW, pad=20)
S.kind("pad", "skirted", d=56, angles=sweep(16), pad=20)
S.kind("sw", "toggle", s=0.6)
S.kind("mode", "chicken", s=0.56, angles=[-50, 0, 50])

KNOBS = [  # id, x, kind, name, (lo, hi), legends (value, text)
    ("input", 420, "trim", "INPUT", (-15, 15), [(-15, "−15"), (0, "0"), (15, "+15")]),
    ("threshold", 565, "big", "THRESHOLD", (-40, 0), [(-40, "−40"), (-30, "30"), (-20, "20"), (-10, "10"), (0, "0")]),
    ("attack", 700, "small", "ATTACK", (0.5, 100), [(0.5, ".5"), (50, "50"), (100, "100")]),
    ("release", 800, "small", "RELEASE", (0.1, 3), [(0.1, ".1"), (1.5, "1.5"), (3, "3")]),
    ("mix", 900, "small", "MIX", (0, 100), [(0, "0"), (50, "50"), (100, "100")]),
    ("thd", 1270, "small", "THD", (0, 8), [(0, "0"), (8, "8")]),
    ("tube", 1370, "small", "TUBE", (-15, 15), [(-15, "−15"), (0, "0"), (15, "+15")]),
    ("output", 1490, "trim", "OUTPUT", (-15, 15), [(-15, "−15"), (0, "0"), (15, "+15")]),
    ("sc_hpf", 1620, "small", "SC HPF", (20, 500), [(20, "20"), (500, "500")]),
    ("sc_bell", 1725, "small", "SC BELL", (2000, 20000), [(2000, "2k"), (20000, "20k")]),
]
SWITCHES = [("limit", 565, "LIMIT"), ("feed_forward", 700, "FF"), ("dynamic_link", 800, "LINK"),
            ("tube_in", 1370, "TUBE"), ("sc_hpf_in", 1620, "HPF"), ("sc_bell_in", 1725, "BELL"), ("sc_listen", 1815, "LISTEN")]
RADII = dict(big=(58, 72), trim=(50, 64), small=(40, 53), pad=(38, 51))


def at(v, lo, hi):
    return -135 + 270 * (v - lo) / (hi - lo)


def row(F, prefix, cy):
    for id, x, kind, name, (lo, hi), legends in KNOBS:
        S.control(prefix + id, kind, F.name, x, cy)
        dr, lr = RADII[kind]
        F.dots(x, cy, dr, sweep(21), major=5, big=kind == "big")
        F.legends(x, cy, lr, [at(v, lo, hi) for v, _ in legends], [t for _, t in legends])
        F.text(name, x, cy + lr + 4, SMALL)
    for id, x, name in SWITCHES:
        S.control(prefix + id, "sw", F.name, x, cy + 118)
        F.text(name, x, cy + 156, TINY)
    F.vu(f"vu_{'side' if prefix else 'main'}", 1080, cy + 10, 236, 150)
    F.text("GAIN REDUCTION", 1080, cy + 104, TINY)


def common(F):
    F.pinstripes()
    F.badge(200, 100, 196, 96, "Beacon")
    F.text("VARI-MU TUBE COMPRESSOR", 200, 166, dict(size=7, ls=2.2))
    S.control("mode", "mode", F.name, 150, 240)
    F.legends(150, 240, 40, [-50, 0, 50], ["DUAL", "STEREO", "M·S"])
    S.control("split", "sw", F.name, 262, 240)
    F.text("SPLIT", 262, 196, TINY)
    F.text("FOLD", 262, 280, TINY)
    # the iron: input transformer, its pad and the stereo link, on the main row only
    S.control("iron_pad", "pad", F.name, 1955, ROW1)
    F.dots(1955, ROW1, 38, sweep(16), major=5)
    F.legends(1955, ROW1, 51, [-135, 135], ["0", "15"])
    F.text("PAD", 1955, ROW1 + 55, SMALL)
    S.control("iron", "sw", F.name, 1925, ROW1 + 118)
    S.control("iron_link", "sw", F.name, 1990, ROW1 + 118)
    F.text("IRON", 1925, ROW1 + 156, TINY)
    F.text("LINK", 1990, ROW1 + 156, TINY)
    F.line(1880, 52, 2040, 52, 0.8)
    F.text("INPUT IRON", 1960, 38, TINY)


folded = S.face("folded", (W, 320))
common(folded)
row(folded, "", ROW1)

opened = S.face("open", (W, 580))
common(opened)
row(opened, "", ROW1)
row(opened, "side_", ROW2)
opened.text("SIDE", 200, ROW2 - 8, dict(size=22, ls=4))
opened.text("OR RIGHT", 200, ROW2 + 22, TINY)
opened.line(110, ROW2 - 82, W - 110, ROW2 - 82, 0.6, alpha=0.6)

S.write(os.path.dirname(os.path.abspath(__file__)))
