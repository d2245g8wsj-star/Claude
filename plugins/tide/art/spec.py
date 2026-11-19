"""
Tide's panel in the range's Nazaré style. python3 spec.py writes spec.json next to it.
Two halves, left (mid) and right (side), around the badge and the centre switches.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "shared", "indiewave", "art"))
from specbuild import Spec, sweep, polar, LABEL, SMALL, TINY

S = Spec("tide", "Tide", lacquer="#b9d9cf")
F = S.face("front", (1800, 520))
F.pinstripes()

FREQ = sweep(6, -125, 125)
GAIN = sweep(11)
S.kind("freq", "skirted", d=74, angles=FREQ, pad=22)
S.kind("gain", "skirted", d=92, angles=GAIN)
S.kind("switch", "toggle", s=0.72)
S.kind("shelf", "toggle", s=0.72, horizontal=True)
S.kind("cue", "chicken", s=0.62, angles=[-50, 0, 50])

TOP, MIDROW, BOTTOM = 140, 262, 375
HALVES = {"a": [185, 357, 529, 701], "b": [1099, 1271, 1443, 1615]}
FREQS = dict(lo=["28", "48", "72", "132", "195", "255"],
             mid=["400", "630", "870", "1k3", "2k4", "4k8"],
             hi=["4k", "7k2", "12k", "16k", "20k", "25k"],
             hpf=["20", "25", "30", "35", "40", "45"])
GAINS = ["4", "3", "2", "1", ".5", "0", ".5", "1", "2", "3", "4"]

# shelf and bell, as printed beside the shelf switches
SHELF = "M0 6 L5 6 C8 6 9 0 12 0 L16 0"
BELL = "M0 6 C4 6 5 0 8 0 C11 0 12 6 16 6"


def freq_knob(prefix, band, x):
    S.control(f"{prefix}_{band}_freq", "freq", "front", x, TOP)
    F.dots(x, TOP, 48, FREQ)
    F.legends(x, TOP, 62, FREQ, FREQS[band])


def gain_knob(prefix, id, x, name):
    S.control(f"{prefix}_{id}", "gain", "front", x, BOTTOM)
    F.dots(x, BOTTOM, 58, GAIN, major=5, big=True)
    F.legends(x, BOTTOM, 72, GAIN, GAINS)
    F.text(name, x, 474, LABEL)
    F.text("−", x - 58, 474, dict(size=12))
    F.text("+", x + 58, 474, dict(size=12))


for prefix, cols in HALVES.items():
    lo, mid, hi, hp = cols
    freq_knob(prefix, "lo", lo); gain_knob(prefix, "lo_gain", lo, "LOW")
    freq_knob(prefix, "mid", mid); gain_knob(prefix, "mid_gain", mid, "MID")
    freq_knob(prefix, "hi", hi); gain_knob(prefix, "hi_gain", hi, "HIGH")
    freq_knob(prefix, "hpf", hp); gain_knob(prefix, "level", hp, "LEVEL")
    F.text("HZ", hp + 52, TOP + 52, TINY)
    # shelf / bell on the low and high bands: right is shelf
    for band, x in (("lo", lo), ("hi", hi)):
        S.control(f"{prefix}_{band}_shelf", "shelf", "front", x, MIDROW)
        F.path(BELL, x - 62, MIDROW - 3, 1.0, 1.4)
        F.path(SHELF if band == "hi" else "M0 0 L4 0 C7 0 8 6 11 6 L16 6", x + 46, MIDROW - 3, 1.0, 1.4)
    # high pass: in, and times five
    for id, dx, up in (("hpf_in", -30, "HPF"), ("hpf_x5", 30, "×5")):
        S.control(f"{prefix}_{id}", "switch", "front", hp + dx, MIDROW)
        F.text(up, hp + dx, MIDROW - 40, TINY)
    title = "LEFT  ·  MID" if prefix == "a" else "RIGHT  ·  SIDE"
    cx = (lo + hp) / 2
    F.text(title, cx, 52, LABEL)
    F.line(lo - 70, 52, cx - 90, 52, 0.8)
    F.line(cx + 90, 52, hp + 70, 52, 0.8)

# centre: the badge, the three switches and the cue selector
F.badge(900, 132, 184, 104, "Tide")
F.text("MASTERING EQUALISER", 900, 204, dict(size=7.5, ls=2.4))
F.text("INDIEWAVE AUDIO", 900, 220, dict(size=7, ls=2.4), alpha=0.75)
for id, x, up, down in (("eq_in", 840, "IN", "OUT"), ("link", 900, "LINK", ""), ("mid_side", 960, "M·S", "L·R")):
    S.control(id, "switch", "front", x, 300)
    F.text(up, x, 258, TINY)
    if down:
        F.text(down, x, 342, TINY)
S.control("cue", "cue", "front", 900, 410)
for a, t in zip([-50, 0, 50], ["L·M", "OFF", "R·S"]):
    x, y = polar(900, 410, 40, a)
    F.dot(x, y, 1.6)
F.legends(900, 410, 54, [-50, 0, 50], ["L·M", "OFF", "R·S"])
F.text("CUE", 900, 474, LABEL)

S.write(os.path.dirname(os.path.abspath(__file__)))
