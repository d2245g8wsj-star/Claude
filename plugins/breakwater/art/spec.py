"""
Breakwater's panel in the range's Nazaré style. python3 spec.py writes spec.json next to it.
Deep teal lacquer with cream print. The simple face: header controls, the scope,
meters and loudness behind glass, and the main row. Advanced adds the limiter,
tone, compressor and converter rows underneath.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "shared", "indiewave", "art"))
from specbuild import Spec, sweep, polar, LABEL, SMALL, TINY

W = 1700
S = Spec("breakwater", "Breakwater", lacquer="#0f343b", ink="#efe6cf")

SW = sweep(61)
STYLE = sweep(8, -105, 105)
S.kind("big", "skirted", d=100, angles=SW)
S.kind("small", "skirted", d=62, angles=SW, pad=20)
S.kind("style", "chicken", s=0.62, angles=STYLE)
S.kind("target", "chicken", s=0.56, angles=STYLE)
S.kind("six", "chicken", s=0.5, angles=sweep(6, -75, 75))
S.kind("four", "chicken", s=0.5, angles=[-66, -22, 22, 66])
S.kind("three", "chicken", s=0.5, angles=[-50, 0, 50])
S.kind("sw", "toggle", s=0.62)
S.kind("pair", "toggle", s=0.56, horizontal=True)


def at(v, lo, hi):
    return -135 + 270 * (v - lo) / (hi - lo)


def section(F, title, x0, x1, y):
    cx = (x0 + x1) / 2
    half = len(title) * 6 + 16
    F.text(title, cx, y, LABEL)
    F.line(x0, y, cx - half, y, 0.8)
    F.line(cx + half, y, x1, y, 0.8)


def knob(F, id, kind, x, y, name, lo, hi, marks):
    S.control(id, kind, F.name, x, y)
    dr, lr = (60, 75) if kind == "big" else (41, 54)
    F.dots(x, y, dr, sweep(21), major=5, big=kind == "big")
    F.legends(x, y, lr, [at(v, lo, hi) for v, _ in marks], [t for _, t in marks])
    F.text(name, x, y + lr + 6, SMALL)


def chooser(F, id, kind, x, y, r, angles, names, title, style=None):
    S.control(id, kind, F.name, x, y)
    for a in angles:
        px, py = polar(x, y, r - 12, a)
        F.dot(px, py, 1.5)
    F.legends(x, y, r, angles, names, style)
    if title:
        F.text(title, x, y + 44, TINY)


def switch(F, id, x, y, name, kind="sw", left=None, right=None):
    S.control(id, kind, F.name, x, y)
    if name:
        F.text(name, x, y + 40, TINY)
    if left:
        F.text(left, x - 30, y, TINY, align="right")
        F.text(right, x + 30, y, TINY, align="left")


def simple(F):
    F.pinstripes()
    F.badge(190, 104, 190, 92, "Breakwater", script_size=30)
    F.text("MASTERING LIMITER", 190, 170, dict(size=7.5, ls=2.4))
    switch(F, "engine", 400, 100, "ENGINE", "pair", "MODERN", "ANALOG")
    chooser(F, "style", "style", 690, 104, 58, STYLE,
            ["CLEAN", "PUNCH", "OPEN", "LOUD", "BALANCED", "GLUE", "SAFE", "SMOOTH"], "")
    F.text("STYLE", 690, 152, TINY)
    for i, (id, name) in enumerate([("true_peak", "TRUE PEAK"), ("bypass", "BYPASS")]):
        switch(F, id, 920 + i * 90, 96, name)
    S.control("advanced", "sw", F.name, 1100, 96)
    F.text("ADVANCED", 1100, 136, TINY)
    chooser(F, "target", "target", 1420, 104, 52, STYLE,
            ["OFF", "−9", "−11", "−14", "−16", "−18", "−23", "−24"], "")
    F.text("TARGET LUFS", 1420, 150, TINY)

    F.window("scope", 86, 190, 1060, 290, 8)
    F.window("meters", 1176, 190, 438, 150, 8)
    F.window("loudness", 1176, 354, 438, 126, 8)

    F.line(90, 508, W - 90, 508, 0.7, alpha=0.6)
    knob(F, "gain", "big", 200, 594, "GAIN", -12, 30, [(-12, "−12"), (0, "0"), (30, "+30")])
    knob(F, "ceiling", "big", 390, 594, "CEILING", -24, 0, [(-24, "−24"), (-12, "12"), (0, "0")])
    knob(F, "transients", "small", 580, 590, "TRANSIENTS", -100, 100, [(-100, "−"), (0, "0"), (100, "+")])
    knob(F, "low_punch", "small", 700, 590, "LOW PUNCH", -100, 100, [(-100, "−"), (0, "0"), (100, "+")])
    knob(F, "detail", "small", 820, 590, "DETAIL", 0, 100, [(0, "0"), (100, "100")])
    switch(F, "match", 980, 580, "GAIN MATCH")
    switch(F, "delta", 1080, 580, "DELTA")
    F.text("MONITOR", 1030, 532, TINY)


def advanced(F):
    y1, y2 = 770, 935
    section(F, "LIMITER", 90, 820, 690)
    knob(F, "lookahead", "small", 150, y1, "LOOKAHEAD", 0, 10, [(0, "0"), (10, "10 ms")])
    knob(F, "attack", "small", 260, y1, "ATTACK", 0.01, 10, [(0.01, "0"), (10, "10")])
    knob(F, "release", "small", 370, y1, "RELEASE", 1, 1000, [(1, "1"), (1000, "1k")])
    chooser(F, "window", "six", 480, y1, 40, sweep(6, -75, 75), ["OFF", "C4", "C3", "C2", "C1", "C0"], "WINDOW")
    knob(F, "link_transients", "small", 590, y1, "LINK TRANS", 0, 100, [(0, "0"), (100, "100")])
    knob(F, "link_release", "small", 700, y1, "LINK REL", 0, 100, [(0, "0"), (100, "100")])
    switch(F, "adaptive_link", 790, y1 - 6, "ADAPTIVE")

    section(F, "TONE", 870, 1240, 690)
    knob(F, "balance", "small", 930, y1, "BALANCE", -100, 100, [(-100, "L"), (0, "0"), (100, "R")])
    knob(F, "clip", "small", 1040, y1, "CLIP", 0, 100, [(0, "0"), (100, "100")])
    switch(F, "dc_filter", 1130, y1 - 6, "DC FILTER")
    switch(F, "sidechain", 1210, y1 - 6, "SIDECHAIN")

    section(F, "CONVERTER", 1290, 1610, 690)
    chooser(F, "oversampling", "six", 1335, y1, 40, sweep(6, -75, 75), ["OFF", "2×", "4×", "8×", "16×", "32×"], "OVERSAMPLING")
    chooser(F, "word_length", "four", 1465, y1, 40, [-66, -22, 22, 66], ["OFF", "24", "20", "16"], "WORD")
    chooser(F, "dither", "three", 1570, y1, 40, [-50, 0, 50], ["NONE", "TPDF", "SHAPED"], "DITHER")

    section(F, "COMPRESSOR", 90, 1000, 850)
    switch(F, "comp_on", 150, y2 - 6, "IN")
    switch(F, "comp_mode", 270, y2, "", "pair", "ST", "M·S")
    F.text("MODE", 270, y2 + 40, TINY)
    knob(F, "comp_threshold", "small", 400, y2, "THRESHOLD", -30, 6, [(-30, "−30"), (6, "+6")])
    knob(F, "comp_ratio", "small", 520, y2, "RATIO", 1, 10, [(1, "1"), (10, "10")])
    knob(F, "comp_attack", "small", 640, y2, "ATTACK", 0.1, 100, [(0.1, ".1"), (100, "100")])
    knob(F, "comp_release", "small", 760, y2, "RELEASE", 10, 2000, [(10, "10"), (2000, "2k")])
    knob(F, "comp_highpass", "small", 880, y2, "SC HPF", 20, 300, [(20, "20"), (300, "300")])
    F.window("comp_gr", 1060, y2 - 34, 180, 60, 6)
    F.text("COMP GR", 1150, y2 + 44, TINY)


simple(S.face("simple", (W, 680)))
adv = S.face("advanced", (W, 1050))
simple(adv)
advanced(adv)

S.write(os.path.dirname(os.path.abspath(__file__)))
