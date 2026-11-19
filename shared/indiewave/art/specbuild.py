"""
Helpers for a plugin's panel spec: one Python file per plugin (plugins/<p>/art/spec.py)
describes the panel with these, and writes spec.json. Everything downstream reads
that one file: the print layers (gen_prints.js), the Blender scene (panel.py), the
packer (assemble.py) and, through panel.json, the editor.

Coordinates are panel points at 1x, origin top left. Angles are degrees clockwise
from twelve o'clock, as on a clock face.
"""
import json, math, os

INK_DEFAULT = "#17222f"


def polar(cx, cy, r, deg):
    a = math.radians(deg - 90)
    return cx + r * math.cos(a), cy + r * math.sin(a)


def sweep(n, a0=-135.0, a1=135.0):
    return [a0 + (a1 - a0) * i / (n - 1) for i in range(n)] if n > 1 else [0.0]


LABEL = dict(size=10.5, ls=3.4)
SMALL = dict(size=8.5, ls=2.4)
TINY = dict(size=7.5, ls=1.6)


class Face:
    def __init__(self, spec, name, size):
        self.spec, self.name, self.size = spec, name, list(size)
        self.ops, self.parts = [], []

    # ---- printing
    def text(self, s, x, y, style=None, **kw):
        o = dict(op="text", s=s, x=x, y=y)
        o.update(style or SMALL); o.update(kw)
        self.ops.append(o)

    def dot(self, x, y, r=1.8, **kw):
        self.ops.append(dict(op="dot", x=x, y=y, r=r, **kw))

    def line(self, x0, y0, x1, y1, w=1.0, **kw):
        self.ops.append(dict(op="line", x0=x0, y0=y0, x1=x1, y1=y1, w=w, **kw))

    def arc(self, cx, cy, r, a0, a1, w=1.0, **kw):
        self.ops.append(dict(op="arc", cx=cx, cy=cy, r=r, a0=a0, a1=a1, w=w, **kw))

    def path(self, d, x, y, s=1.0, w=1.6, **kw):
        self.ops.append(dict(op="path", d=d, x=x, y=y, s=s, w=w, **kw))

    def iw(self, x, y, s):
        self.ops.append(dict(op="iw", x=x, y=y, s=s))

    def pinstripes(self, inset=20):
        w, h = self.size
        x0, x1 = self.spec.ears + inset, w - self.spec.ears - inset
        for y in (22.5, 26.5, h - 26.5, h - 22.5):
            self.line(x0, y, x1, y, 1.0)

    def dots(self, cx, cy, r, angles, major=None, big=False, minor_alpha=0.55):
        for i, a in enumerate(angles):
            x, y = polar(cx, cy, r, a)
            m = major is None or i % major == 0 or i == len(angles) - 1
            self.dot(x, y, (2.2 if big else 1.7) if m else 1.15, alpha=1 if m else minor_alpha)

    def legends(self, cx, cy, r, angles, texts, style=None, **kw):
        """Text at angles round a control; left of centre right-aligned, and so on."""
        for a, t in zip(angles, texts):
            if not t:
                continue
            x, y = polar(cx, cy, r, a)
            align = "right" if a < -8 and a > -172 else "left" if 8 < a < 172 else "center"
            self.text(t, x, y, style or TINY, align=align, **kw)

    # ---- fixed parts on the plate
    def badge(self, cx, cy, w, h, script, sub=None, script_size=40):
        layer = f"badge_{self.name}_{len(self.parts)}"
        ops = [dict(op="iw", x=w / 2 - 15, y=12, s=0.75),
               dict(op="text", s=script, x=w / 2, y=h / 2 + 12, font="Yellowtail", weight=400, size=script_size)]
        if sub:
            ops.append(dict(op="text", s=sub, x=w / 2, y=h - 20, size=7, ls=2.0))
        self.spec.layers[layer] = dict(w=w, h=h, ops=ops)
        self.parts.append(dict(type="badge", cx=cx, cy=cy, w=w, h=h, layer=layer))

    def window(self, id, x, y, w, h, radius=8):
        """A black glass window behind a chrome rim, for a display the editor draws."""
        self.parts.append(dict(type="window", id=id, x=x, y=y, w=w, h=h, r=radius))

    def vu(self, id, cx, cy, w=250, h=160, script=None):
        layer = f"vu_{self.name}_{id}"
        fw, fh = w - 24, h - 24
        self.spec.layers[layer] = dict(w=fw, h=fh, bg="#f3e7c8", vu=dict(script=script or self.spec.title))
        self.parts.append(dict(type="vu", id=id, cx=cx, cy=cy, w=w, h=h, face=[fw, fh], layer=layer,
                               pivot=[fw / 2, fh + 30], r=fh * 0.92))

    def plaque(self, x, y, w, h, rows, script=None):
        layer = f"plaque_{self.name}_{len(self.parts)}"
        ops = [dict(op="text", s=script or self.spec.title, x=22, y=38, font="Yellowtail", weight=400, size=32, align="left"),
               dict(op="iw", x=w - 64, y=16, s=1.0),
               dict(op="rect", x=22, y=68, wd=w - 44, ht=0.8, fill=True)]
        for i, (a, b) in enumerate(rows):
            ops.append(dict(op="text", s=a, x=22, y=92 + i * 22, size=8.5, ls=2.2, align="left"))
            ops.append(dict(op="text", s=b, x=w - 22, y=92 + i * 22, size=8.5, ls=2.2, align="right"))
        ops.append(dict(op="rect", x=22, y=92 + len(rows) * 22 - 4, wd=w - 44, ht=0.8, fill=True))
        self.spec.layers[layer] = dict(w=w, h=h, ops=ops)
        self.parts.append(dict(type="plaque", x=x, y=y, w=w, h=h, layer=layer))


class Spec:
    def __init__(self, name, title, lacquer, ink=INK_DEFAULT, ears=56, ear_metal="nickel"):
        self.name, self.title, self.lacquer, self.ink, self.ears = name, title, lacquer, ink, ears
        self.ear_metal = ear_metal
        self.faces, self.kinds, self.controls, self.layers = {}, {}, [], {}

    def face(self, name, size):
        f = Face(self, name, size)
        self.faces[name] = f
        return f

    def kind(self, name, type, **params):
        """A kind of moving part, rendered once as a filmstrip and used by every control of that kind."""
        self.kinds[name] = dict(type=type, **params)

    def control(self, id, kind, face, cx, cy, **extra):
        self.controls.append(dict(id=id, kind=kind, face=face, cx=cx, cy=cy, **extra))

    def write(self, art_dir):
        for f in self.faces.values():
            self.layers["face_" + f.name] = dict(w=f.size[0], h=f.size[1], ops=f.ops)
        out = dict(name=self.name, title=self.title, lacquer=self.lacquer, ink=self.ink, ears=self.ears,
                   ear_metal=self.ear_metal,
                   faces={k: dict(size=f.size, parts=f.parts) for k, f in self.faces.items()},
                   kinds=self.kinds, controls=self.controls, layers=self.layers)
        os.makedirs(art_dir, exist_ok=True)
        with open(os.path.join(art_dir, "spec.json"), "w") as fh:
            json.dump(out, fh, indent=1)
        print("wrote", os.path.join(art_dir, "spec.json"))
