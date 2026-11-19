"""
Packs a plugin's Blender frames into what its editor compiles in, and writes a
composite of each face for checking.

    python assemble.py plugins/tide/art plugins/tide/resources/panel

  <out>/plate_<face>.jpg   each face, without its moving parts
  <out>/<kind>.png         each kind's frames on one sheet, 8 to a row
  <out>/panel.json         sizes, positions and sheet layout for the editor
  <art>/renders/composite_<face>.png   every control at a middle setting
"""
import glob, json, os, sys
import numpy as np
from PIL import Image

ART, OUT = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
SPEC = json.load(open(os.path.join(ART, "spec.json")))
FR = os.path.join(ART, "renders", "frames")
os.makedirs(OUT, exist_ok=True)


def clean(img):
    """Shadow-catcher frames carry a faint noise floor across the whole crop.
    Lift it, and fade the shadow out towards the crop edges so no seam shows."""
    a = np.asarray(img).astype(np.float32)
    al = np.clip((a[..., 3] - 6.0) * 255.0 / 249.0, 0, 255)
    h, w = al.shape
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy)) / (0.14 * min(w, h))
    al *= np.clip(d, 0, 1) ** 1.5
    a[..., 3] = al
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def frames(key):
    return [clean(Image.open(p).convert("RGBA")) for p in sorted(glob.glob(os.path.join(FR, f"{key}_[0-9][0-9][0-9].png")))]


first_face = next(iter(SPEC["faces"]))
k = Image.open(os.path.join(FR, f"plate_{first_face}.png")).width / SPEC["faces"][first_face]["size"][0]
meta = dict(name=SPEC["name"], scale=k, faces={}, kinds={}, controls=[], parts={})

for fname, face in SPEC["faces"].items():
    Image.open(os.path.join(FR, f"plate_{fname}.png")).convert("RGB").save(
        os.path.join(OUT, f"plate_{fname}.jpg"), "JPEG", quality=90, subsampling=0, optimize=True)
    meta["faces"][fname] = dict(size=face["size"])
    meta["parts"][fname] = [p for p in face["parts"] if p["type"] in ("window", "vu")]

sheets = {}
for kname, K in SPEC["kinds"].items():
    fs = frames(kname)
    if not fs:
        print("no frames for", kname); continue
    w, h = fs[0].size
    cols = min(len(fs), 8); rows = (len(fs) + cols - 1) // cols
    sheet = Image.new("RGBA", (w * cols, h * rows))
    for i, f in enumerate(fs): sheet.paste(f, ((i % cols) * w, (i // cols) * h))
    sheet.save(os.path.join(OUT, f"{kname}.png"), optimize=True)
    sheets[kname] = fs
    meta["kinds"][kname] = dict(type=K["type"], w=w / k, h=h / k, n=len(fs), cols=cols,
                                angles=K.get("angles"), steps=K.get("steps"))

meta["controls"] = SPEC["controls"]
json.dump(meta, open(os.path.join(OUT, "panel.json"), "w"), indent=1)

for fname in SPEC["faces"]:
    comp = Image.open(os.path.join(FR, f"plate_{fname}.png")).convert("RGBA")
    for c in SPEC["controls"]:
        if c["face"] != fname or c["kind"] not in sheets: continue
        fs = sheets[c["kind"]]
        f = fs[len(fs) // 2]
        comp.alpha_composite(f, (int(round(c["cx"] * k - f.width / 2)), int(round(c["cy"] * k - f.height / 2))))
    comp.convert("RGB").save(os.path.join(ART, "renders", f"composite_{fname}.png"))
    print("composite", fname)

print("sizes:", {f: os.path.getsize(os.path.join(OUT, f)) // 1024 for f in os.listdir(OUT)})
