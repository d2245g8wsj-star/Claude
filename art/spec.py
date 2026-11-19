"""
Crest's panel in the range's Nazaré style. python3 art/spec.py writes art/spec.json.
The badge and the IN/OUT meter on the left; CLIP; the display window with the
waveform, the transfer curve and four figures; TONE; OUTPUT; the converter row
along the bottom.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "shared", "indiewave", "art"))
from specbuild import Spec, sweep, polar, LABEL, SMALL, TINY

W, H = 1880, 560
S = Spec("crest", "Crest", lacquer="#bcd3e8")
F = S.face("front", (W, H))
F.pinstripes()

SW = sweep(61)
S.kind("big", "skirted", d=92, angles=SW)
S.kind("small", "skirted", d=62, angles=SW, pad=20)
S.kind("char", "chicken", s=0.62, angles=[-66, -22, 22, 66])
S.kind("os", "chicken", s=0.5, angles=[-80, -40, 0, 40, 80])
S.kind("word", "chicken", s=0.5, angles=[-66, -22, 22, 66])
S.kind("dither", "chicken", s=0.5, angles=[-50, 0, 50])
S.kind("sw", "toggle", s=0.62)
S.kind("pilot", "jewel", colour="red", s=0.8)


def at(v, lo, hi):
    return -135 + 270 * (v - lo) / (hi - lo)


def knob(id, kind, x, y, name, lo, hi, marks):
    S.control(id, kind, "front", x, y)
    dr, lr = (56, 70) if kind == "big" else (41, 54)
    F.dots(x, y, dr, sweep(21), major=5, big=kind == "big")
    F.legends(x, y, lr, [at(v, lo, hi) for v, _ in marks], [t for _, t in marks])
    F.text(name, x, y + lr + 6, SMALL)


def selector(id, kind, x, y, r, angles, names, title):
    S.control(id, kind, "front", x, y)
    for a in angles:
        px, py = polar(x, y, r - 12, a)
        F.dot(px, py, 1.6)
    F.legends(x, y, r, angles, names)
    F.text(title, x, y + 40, TINY)


def section(title, x0, x1, y=52):
    cx = (x0 + x1) / 2
    half = len(title) * 6 + 16
    F.text(title, cx, y, LABEL)
    F.line(x0, y, cx - half, y, 0.8)
    F.line(cx + half, y, x1, y, 0.8)


# badge, pilot and the IN/OUT meter
F.badge(200, 112, 196, 100, "Crest")
F.text("CONVERTER  ·  CLIPPER", 200, 182, dict(size=7.5, ls=2.2))
F.vu("vu", 200, 320, 230, 146)
F.text("IN", 150, 412, TINY)
F.line(162, 412, 178, 412, 1.6)
F.text("OUT", 222, 412, TINY)
F.line(240, 412, 256, 412, 1.6, color="#b8352a")
S.control("pilot", "pilot", "front", 200, 470)
F.text("POWER", 200, 500, TINY)

# CLIP
section("CLIP", 370, 760)
knob("input", "big", 455, 150, "INPUT", -12, 18, [(-12, "−12"), (0, "0"), (18, "+18")])
selector("mode", "char", 655, 150, 54, [-66, -22, 22, 66], ["GLASS", "EDGE", "SILK", "TUBE"], "CHARACTER")
knob("knee", "small", 430, 330, "KNEE", 0, 24, [(0, "0"), (24, "24")])
knob("adapt", "small", 560, 330, "ADAPT", 0, 100, [(0, "0"), (100, "100")])
knob("bias", "small", 690, 330, "BIAS", -100, 100, [(-100, "−"), (0, "0"), (100, "+")])

# the display: waveform with the transfer curve inset, and four figures
F.window("display", 800, 76, 500, 262, 8)
for i, name in enumerate(["CLIP dB", "MOMENTARY", "SHORT TERM", "INTEGRATED"]):
    x = 800 + i * 128
    F.window(f"figure{i}", x, 368, 116, 38, 5)
    F.text(name, x + 58, 422, TINY)
F.text("WAVEFORM", 808, 60, TINY, align="left")

# TONE
section("TONE", 1330, 1600)
knob("colour", "small", 1395, 150, "COLOUR", 0, 100, [(0, "0"), (100, "100")])
knob("harmonics", "small", 1535, 150, "HARMONICS", 0, 100, [(0, "0"), (100, "100")])
knob("series", "small", 1465, 262, "SERIES", -100, 100, [(-100, "EVEN"), (100, "ODD")])
knob("sclpf", "small", 1395, 372, "SC LPF", 15, 800, [(15, "OFF"), (800, "800")])
knob("mix", "small", 1535, 372, "MIX", 0, 100, [(0, "0"), (100, "100")])

# OUTPUT
section("OUTPUT", 1630, 1790)
knob("ceiling", "big", 1710, 160, "CEILING", -24, 0, [(-24, "−24"), (-12, "12"), (0, "0")])
knob("output", "big", 1710, 370, "OUTPUT", -24, 12, [(-24, "−24"), (0, "0"), (12, "+12")])

# the converter row
F.line(380, 448, 1600, 448, 0.7, alpha=0.6)
selector("os", "os", 450, 492, 34, [-80, -40, 0, 40, 80], ["OFF", "2×", "4×", "8×", "16×"], "")
F.text("OVERSAMPLING", 450, 528, TINY)
selector("word", "word", 600, 492, 34, [-66, -22, 22, 66], ["OFF", "24", "20", "16"], "")
F.text("WORD", 600, 528, TINY)
selector("dither", "dither", 740, 492, 34, [-50, 0, 50], ["OFF", "TPDF", "SHAPED"], "")
F.text("DITHER", 740, 528, TINY)
for i, (id, name) in enumerate([("autogain", "AUTO"), ("tpsafe", "TP SAFE"), ("delta", "DELTA"), ("link", "LINK"), ("bypass", "BYPASS")]):
    x = 900 + i * 100
    S.control(id, "sw", "front", x, 488)
    F.text(name, x, 528, TINY)

S.write(os.path.dirname(os.path.abspath(__file__)))
