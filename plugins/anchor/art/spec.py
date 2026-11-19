"""
Anchor's panel in the range's Nazaré style. python3 spec.py writes spec.json next to it.
A half per channel either side of the centre (badge, mode, split, iron): folded,
one half and the centre for stereo; open, the side (or right) half beside it.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "shared", "indiewave", "art"))
from specbuild import Spec, sweep, polar, LABEL, SMALL, TINY

H = 560
HALF, CENTRE = 760, 240
S = Spec("anchor", "Anchor", lacquer="#d3c7e3")

SW = sweep(61)
S.kind("big", "skirted", d=100, angles=SW)
S.kind("trim", "skirted", d=78, angles=SW)
S.kind("small", "skirted", d=58, angles=SW, pad=20)
S.kind("ratio", "skirted", d=66, angles=sweep(5, -100, 100), pad=22)
S.kind("eight", "skirted", d=66, angles=sweep(8, -120, 120), pad=22)
S.kind("six", "skirted", d=56, angles=sweep(6, -100, 100), pad=20)
S.kind("pad", "skirted", d=52, angles=sweep(16), pad=20)
S.kind("sw", "toggle", s=0.6)
S.kind("mode", "chicken", s=0.56, angles=[-50, 0, 50])


def at(v, lo, hi):
    return -135 + 270 * (v - lo) / (hi - lo)


def knob(F, id, kind, x, y, name, marks=None, lohi=None, steps=None):
    S.control(id, kind, F.name, x, y)
    K = S.kinds[kind]
    r = K["d"] / 2
    dr, lr = r + 12, r + 25
    if steps:
        F.dots(x, y, dr, K["angles"])
        F.legends(x, y, lr, K["angles"], steps)
    else:
        F.dots(x, y, dr, sweep(21), major=5, big=kind == "big")
        lo, hi = lohi
        F.legends(x, y, lr, [at(v, lo, hi) for v, _ in marks], [t for _, t in marks])
    F.text(name, x, y + lr + 8, SMALL)


def switch(F, id, x, y, name):
    S.control(id, "sw", F.name, x, y)
    F.text(name, x, y + 38, TINY)


def half(F, x0, prefix, numeral):
    p = lambda n: prefix + n
    F.text(numeral, x0 + HALF / 2, 52, LABEL)
    F.line(x0 + 20, 52, x0 + HALF / 2 - 40, 52, 0.8)
    F.line(x0 + HALF / 2 + 40, 52, x0 + HALF - 20, 52, 0.8)
    knob(F, p("threshold"), "big", x0 + 110, 150, "THRESHOLD", [(-20, "−20"), (0, "0"), (20, "+20")], (-20, 20))
    F.vu(f"vu_{'side' if prefix else 'main'}", x0 + 380, 150, 236, 150)
    F.text("GAIN REDUCTION", x0 + 380, 240, TINY)
    knob(F, p("mix"), "trim", x0 + 650, 150, "MIX", [(0, "DRY"), (100, "WET")], (0, 100))
    knob(F, p("attack"), "eight", x0 + 110, 320, "ATTACK", steps=[".1", ".3", "1", "3", "5", "10", "15", "30"])
    switch(F, p("mute"), x0 + 225, 310, "MUTE")
    knob(F, p("ratio"), "ratio", x0 + 380, 320, "RATIO", steps=["1.5", "2", "4", "10", "INF"])
    switch(F, p("feedback"), x0 + 520, 310, "FEEDBACK")
    knob(F, p("release"), "eight", x0 + 650, 320, "RELEASE", steps=["50", ".1", ".3", ".5", ".6", ".9", "1.2", "A"])
    knob(F, p("thd"), "small", x0 + 110, 455, "THD", [(0, "0"), (100, "100")], (0, 100))
    knob(F, p("make_up"), "small", x0 + 270, 455, "MAKE UP", [(0, "0"), (20, "20")], (0, 20))
    S.control(p("make_up_iron"), "small", F.name, x0 + 270, 440)
    knob(F, p("sc_filter"), "six", x0 + 560, 455, "SC FILTER", steps=["OFF", "60", "90", "150", "T1", "T2"])
    switch(F, p("sc_listen"), x0 + 665, 445, "LISTEN")
    if not prefix:
        knob(F, "iron_pad", "pad", x0 + 420, 455, "IRON PAD", [(0, "0"), (15, "15")], (0, 15))
        switch(F, "iron_link", x0 + 345, 445, "LINK")
        switch(F, "sc_external", x0 + 725, 445, "EXT KEY")


def centre(F, cx):
    F.line(cx - CENTRE / 2, 36, cx - CENTRE / 2, H - 36, 0.7, alpha=0.5)
    F.line(cx + CENTRE / 2, 36, cx + CENTRE / 2, H - 36, 0.7, alpha=0.5)
    F.badge(cx, 108, 186, 96, "Anchor")
    F.text("VCA BUS COMPRESSOR", cx, 176, dict(size=7, ls=2.2))
    S.control("mode", "mode", F.name, cx - 40, 270)
    F.legends(cx - 40, 270, 40, [-50, 0, 50], ["DUAL", "STEREO", "M·S"])
    S.control("split", "sw", F.name, cx + 62, 270)
    F.text("SPLIT", cx + 62, 228, TINY)
    F.text("FOLD", cx + 62, 310, TINY)
    switch(F, "iron", cx - 45, 410, "IRON  RED")
    switch(F, "bypass", cx + 45, 410, "BYPASS")


def face(name, open_):
    w = 56 + 24 + HALF + CENTRE + (HALF if open_ else 0) + 24 + 56
    F = S.face(name, (w, H))
    F.pinstripes()
    x0 = 80
    half(F, x0, "", "MID  ·  LEFT" if open_ else "")
    cx = x0 + HALF + CENTRE / 2
    centre(F, cx)
    if open_:
        half(F, x0 + HALF + CENTRE, "side_", "SIDE  ·  RIGHT")


face("folded", False)
face("open", True)
S.write(os.path.dirname(os.path.abspath(__file__)))
