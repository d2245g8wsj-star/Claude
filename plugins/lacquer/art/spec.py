"""
Lacquer's panel in the range's Nazaré style. python3 spec.py writes spec.json next to it.
A row per channel, frequency over amount, the band colours in the dots as on the
hardware; the bar under the first row carries the badge, the mode and the output.
Four faces: folded and open, each in Classic White and Lacquer Black.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "shared", "indiewave", "art"))
from specbuild import Spec, sweep, polar, LABEL, SMALL, TINY

W = 1600
S = Spec("lacquer", "Lacquer", lacquer="#efe9dc")

FINISHES = {
    "white": dict(lacquer="#efe9dc", ink="#2b2926",
                  bands=dict(bb="#c3342c", bc="#82231d", mb="#e0961f", mc="#3c955a", tb="#2f5fb3", ts="#7a49ae", tc="#23899a")),
    "black": dict(lacquer="#0d0c0e", ink="#ece6d6",
                  bands=dict(bb="#e8463b", bc="#b8564c", mb="#f2aa33", mc="#52c478", tb="#5c8fe6", ts="#a57ddc", tc="#3bb6c7")),
}

F6, F11, F12, A21, Q11 = sweep(6, -120, 120), sweep(11), sweep(12), sweep(21), sweep(11, -120, 120)
S.kind("f6", "skirted", d=58, angles=F6, pad=20)
S.kind("f11", "skirted", d=58, angles=F11, pad=20)
S.kind("f12", "skirted", d=58, angles=F12, pad=20)
S.kind("amount", "skirted", d=84, angles=A21)
S.kind("q", "skirted", d=46, angles=Q11, pad=18)
S.kind("sw", "toggle", s=0.74)
S.kind("output", "skirted", d=62, angles=sweep(61), pad=20)
S.kind("mode", "chicken", s=0.56, angles=[-50, 0, 50])
S.kind("pilot", "jewel", colour="red", s=0.85)
S.kind("finish", "toggle", s=0.56, horizontal=True)

COLS = dict(lb=170, lc=300, mb=460, mc=590, tb=750, q=860, tc=970)
TOGGLES = [1110, 1190, 1270, 1350]

# the band curves printed over each pair, and the switch icons
GLYPH = dict(boost="M0 5 L10 5 C16 5 16 -5 22 -5 L32 -5", cut="M0 -5 L10 -5 C16 -5 16 5 22 5 L32 5",
             bell="M0 5 C10 5 11 -7 16 -7 C21 -7 22 5 32 5", dip="M0 -5 C10 -5 11 7 16 7 C21 7 22 -5 32 -5")
SHIFT = ["M0 -6 L9 -6 C14 -6 15 6 21 6 L30 6", "M6 -6 L15 -6 C20 -6 21 6 27 6 L36 6"]
BUFFER = ["M0 0 L9 0 M27 0 L36 0", "M9 -9 L27 0 L9 9 Z"]
POWER = ["M11.5 -5.5 A9 9 0 1 0 24.5 -5.5", "M18 -12 L18 -1"]


def iron():
    d = ""
    for sx in (-1, 1):
        for j in range(4):
            x, y = 18 + sx * 9, -10 + j * 5
            d += f"M{x} {y} a3 2.5 0 0 {1 if sx > 0 else 0} 0 5 "
    return [d, "M16 -11 L16 11 M20 -11 L20 11"]


def ramp(F, cx, cy, r, n, colour):
    for i, a in enumerate(sweep(n)):
        x, y = polar(cx, cy, r, a)
        F.dot(x, y, 0.9 + 1.6 * i / (n - 1), color=colour)


def row(F, fin, prefix, top, numeral):
    c = FINISHES[fin]["bands"]
    fy, qy, ay = top + 70, top + 128, top + 185

    def band(key, fkind, angles, colour, glyph, freq_id, amount_id, out=False):
        x = COLS[key]
        F.path(GLYPH[glyph], x - 16, top + 12, 1.0, 2.0, color=colour)
        S.control(f"{prefix}_{freq_id}", fkind, F.name, x, fy)
        for i, a in enumerate(angles):
            px, py = polar(x, fy, 40, a)
            if out and i == 0:
                F.line(px - 3, py, px + 3, py, 1.6, color=FINISHES[fin]["ink"])
            else:
                F.dot(px, py, 2.0, color=colour)
        S.control(f"{prefix}_{amount_id}", "amount", F.name, x, ay)
        ramp(F, x, ay, 54, 21, colour)

    band("lb", "f6", F6, c["bb"], "boost", "lb_freq", "lb_amount")
    band("lc", "f6", F6, c["bc"], "cut", "lc_freq", "lc_amount")
    band("mb", "f11", F11, c["mb"], "bell", "mb_freq", "mb_amount")
    band("mc", "f11", F11, c["mc"], "dip", "mc_freq", "mc_amount", out=True)
    band("tb", "f12", F12, c["tb"], "bell", "tb_freq", "tb_amount")
    band("tc", "f6", F6, c["tc"], "cut", "tc_freq", "tc_amount")
    # treble boost: bells, then the shelves in their own colour
    for i, a in enumerate(F12):
        if i >= 7:
            px, py = polar(COLS["tb"], fy, 40, a)
            F.dot(px, py, 2.0, color=c["ts"])
    x = COLS["q"]
    S.control(f"{prefix}_tb_q", "q", F.name, x, qy)
    F.path("M0 6 C5 6 6 -6 11 -6 C16 -6 17 6 22 6", x - 11, qy - 50, 1.0, 1.4)
    for i, a in enumerate(Q11):
        px, py = polar(x, qy, 33, a)
        F.dot(px, py, 0.9 + 0.9 * i / 10)

    for xm in ((COLS["lc"] + COLS["mb"]) / 2, (COLS["mc"] + COLS["tb"]) / 2, (COLS["tc"] + TOGGLES[0]) / 2):
        F.line(xm, top + 30, xm, top + 232, 0.7, alpha=0.5)

    icons = [(SHIFT, c["bb"]), (BUFFER, None), (iron(), None), (POWER, None)]
    for (paths, colour), x, id in zip(icons, TOGGLES, ("shift", "buffer", "transformer", "eq_in")):
        for d in paths:
            F.path(d, x - 18, top + 82, 1.0, 1.6, **({"color": colour} if colour else {}))
        S.control(f"{prefix}_{id}", "sw", F.name, x, top + 150)
    F.text(numeral, 1452, top + 150, dict(size=28, ls=0))


def bar(F, fin, y):
    """The bar between the rows: pilot, finish, the badge, mode and output."""
    F.line(80, y - 48, W - 80, y - 48, 0.7, alpha=0.6)
    F.line(80, y + 48, W - 80, y + 48, 0.7, alpha=0.6)
    S.control("pilot", "pilot", F.name, 150, y)
    F.path(POWER[0] + " " + POWER[1], 172, y + 3, 0.8, 1.6)
    S.control("finish", "finish", F.name, 330, y)
    F.text("WHITE", 280, y, TINY, align="right")
    F.text("BLACK", 380, y, TINY, align="left")
    F.text("FINISH", 330, y + 30, TINY)
    F.badge(800, y, 176, 84, "Lacquer", script_size=34)
    S.control("mode", "mode", F.name, 1130, y + 6)
    F.legends(1130, y + 6, 40, [-50, 0, 50], ["STEREO", "DUAL", "M·S"])
    S.control("output", "output", F.name, 1330, y)
    for v, t in ((-12, "−12"), (0, "0"), (12, "+12")):
        a = -135 + 270 * (v + 12) / 24
        px, py = polar(1330, y, 44, a)
        F.text(t, px + (-6 if v < 0 else 6 if v > 0 else 0), py, TINY)
    F.dots(1330, y, 36, sweep(25), major=6)
    F.text("OUTPUT", 1420, y, TINY, align="left")


for fin, look in FINISHES.items():
    folded = S.face(f"folded_{fin}", (W, 430), lacquer=look["lacquer"], ink=look["ink"])
    folded.pinstripes()
    row(folded, fin, "c1", 50, "")
    bar(folded, fin, 350)

    opened = S.face(f"open_{fin}", (W, 700), lacquer=look["lacquer"], ink=look["ink"])
    opened.pinstripes()
    row(opened, fin, "c1", 50, "I")
    bar(opened, fin, 350)
    row(opened, fin, "c2", 410, "II")

S.write(os.path.dirname(os.path.abspath(__file__)))
