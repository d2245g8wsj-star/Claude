"""
Lantern's panel in the range's Nazaré style. python3 spec.py writes spec.json next to it.
A half per channel either side of the centre (badge, mode, split, the response
behind glass, iron): folded, one half and the centre for stereo; open, the side
(or right) half beside it. Four bands a half, in their colours.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "shared", "indiewave", "art"))
from specbuild import Spec, sweep, polar, LABEL, SMALL, TINY

H = 660
HALF, CENTRE = 830, 300
S = Spec("lantern", "Lantern", lacquer="#e8d6b4")

SW = sweep(61)
TWELVE = sweep(12)
S.kind("gain", "skirted", d=74, angles=SW)
S.kind("freq", "skirted", d=54, angles=TWELVE, pad=20)
S.kind("q", "skirted", d=48, angles=SW, pad=18)
S.kind("small", "skirted", d=52, angles=SW, pad=20)
S.kind("pad", "skirted", d=50, angles=sweep(16), pad=20)
S.kind("sw", "toggle", s=0.56)
S.kind("listen", "chicken", s=0.5, angles=sweep(5, -80, 80))
S.kind("mode", "chicken", s=0.56, angles=[-50, 0, 50])

BANDS = [("lf", "LF", "#b8382c", ["22", "33", "60", "100", "150", "220", "300", "410", "500", "730", "860", "1k"]),
         ("lmf", "LMF", "#c9821c", ["90", "120", "180", "290", "430", "620", "1k", "1k2", "1k9", "2k7", "3k9", "4k8"]),
         ("hmf", "HMF", "#2c8a57", ["260", "380", "550", "970", "1k2", "1k7", "2k8", "3k8", "5k5", "7k8", "9k", "11k"]),
         ("hf", "HF", "#2a64a8", [".6k", ".8k", "1k2", "1k8", "2k8", "4k1", "6k", "8k2", "10k", "13k", "18k", "28k"])]


def at(v, lo, hi):
    return -135 + 270 * (v - lo) / (hi - lo)


def swept(F, id, kind, x, y, name, lo, hi, marks, colour=None):
    S.control(id, kind, F.name, x, y)
    r = S.kinds[kind]["d"] / 2
    for i, a in enumerate(sweep(21)):
        px, py = polar(x, y, r + 11, a)
        m = i % 5 == 0 or i == 20
        F.dot(px, py, 1.7 if m else 1.1, **({"color": colour} if colour else {}), alpha=1 if m else 0.6)
    F.legends(x, y, r + 23, [at(v, lo, hi) for v, _ in marks], [t for _, t in marks])
    if name:
        F.text(name, x, y + r + 31, SMALL)


def switch(F, id, x, y, name, below=True):
    S.control(id, "sw", F.name, x, y)
    F.text(name, x, y + (36 if below else -36), TINY)


def half(F, x0, prefix, title):
    p = lambda n: prefix + n
    if title:
        F.text(title, x0 + HALF / 2, 52, LABEL)
        F.line(x0 + 20, 52, x0 + HALF / 2 - 70, 52, 0.8)
        F.line(x0 + HALF / 2 + 70, 52, x0 + HALF - 20, 52, 0.8)
    for i, (band, name, colour, freqs) in enumerate(BANDS):
        x = x0 + 90 + i * 168
        F.text(name, x - 52, 86, dict(size=10, ls=2.4), color=colour, align="left")
        switch(F, p(f"{band}_in"), x + 52, 92, "IN", below=False)
        swept(F, p(f"{band}_gain"), "gain", x, 160, "GAIN", -15, 15, [(-15, "−"), (0, "0"), (15, "+")], colour)
        S.control(p(f"{band}_freq"), "freq", F.name, x, 290)
        for a in TWELVE:
            px, py = polar(x, 290, 38, a)
            F.dot(px, py, 1.8, color=colour)
        F.legends(x, 290, 50, [TWELVE[0], TWELVE[11]], [freqs[0], freqs[11]])
        F.text("FREQ", x, 350, SMALL)
        swept(F, p(f"{band}_q"), "q", x - 12, 410, "", 0.5, 4, [(0.5, "WIDE"), (4, "NARROW")])
        F.text("Q", x - 12, 452, SMALL)
        switch(F, p(f"{band}_shelf"), x + 58, 412, "SHELF", below=False)
        if i < 3:
            F.line(x + 84, 80, x + 84, 470, 0.6, alpha=0.4)
    # the right hand column: range, Q mode, mute, listen
    xr = x0 + HALF - 60
    switch(F, p("range"), xr, 120, "15 dB", below=False)
    F.text("5 dB", xr, 156, TINY)
    switch(F, p("q_mode"), xr, 230, "PROP Q", below=False)
    F.text("CONST", xr, 266, TINY)
    switch(F, p("mute"), xr, 330, "MUTE")
    S.control(p("listen"), "listen", F.name, xr, 430)
    F.legends(xr, 430, 36, sweep(5, -80, 80), ["", "LF", "LMF", "HMF", "HF"])
    F.text("OFF", xr - 40, 450, TINY, align="right")
    F.text("LISTEN", xr, 470, TINY)
    # filters, saturation and the output stage
    F.line(x0 + 20, 500, x0 + HALF - 20, 500, 0.7, alpha=0.6)
    y = 572
    swept(F, p("hpf"), "small", x0 + 70, y, "HPF", 12, 300, [(12, "12"), (300, "300")])
    switch(F, p("hpf_in"), x0 + 140, y - 10, "IN")
    switch(F, p("hpf_24"), x0 + 190, y - 10, "24")
    swept(F, p("lpf"), "small", x0 + 270, y, "LPF", 8000, 50000, [(8000, "8k"), (50000, "50k")])
    switch(F, p("lpf_in"), x0 + 340, y - 10, "IN")
    switch(F, p("lpf_24"), x0 + 390, y - 10, "24")
    swept(F, p("thd"), "small", x0 + 480, y, "THD", 0, 100, [(0, "0"), (100, "100")])
    swept(F, p("output"), "small", x0 + 600, y, "OUTPUT", -15, 15, [(-15, "−15"), (15, "+15")])
    S.control(p("output_iron"), "small", F.name, x0 + 600, y)
    if not prefix:
        swept(F, "iron_pad", "pad", x0 + 720, y, "IRON PAD", 0, 15, [(0, "0"), (15, "15")])
        switch(F, "iron_link", x0 + 790, y - 10, "LINK")


def centre(F, cx):
    F.line(cx - CENTRE / 2, 36, cx - CENTRE / 2, H - 36, 0.7, alpha=0.5)
    F.line(cx + CENTRE / 2, 36, cx + CENTRE / 2, H - 36, 0.7, alpha=0.5)
    F.badge(cx, 104, 190, 96, "Lantern")
    F.text("PASSIVE TUBE EQUALISER", cx, 170, dict(size=7, ls=2.2))
    S.control("mode", "mode", F.name, cx - 50, 250)
    F.legends(cx - 50, 250, 40, [-50, 0, 50], ["DUAL", "STEREO", "M·S"])
    S.control("split", "sw", F.name, cx + 70, 250)
    F.text("SPLIT", cx + 70, 210, TINY)
    F.text("FOLD", cx + 70, 288, TINY)
    F.window("curve", cx - 125, 320, 250, 150, 8)
    F.text("RESPONSE", cx, 484, TINY)
    switch(F, "iron", cx - 50, 560, "IRON  RED")
    switch(F, "bypass", cx + 50, 560, "BYPASS")


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
