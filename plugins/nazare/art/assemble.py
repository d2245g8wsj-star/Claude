"""
Packs the Blender frames into what the editor loads, and writes a composite
for checking: renders/frames/*.png -> renders/*.webp + renders/composite_*.png

    python3 assemble.py
"""
import glob, json, os, re
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
L = json.load(open(os.path.join(HERE, "layout.json")))
FR = os.path.join(HERE, "renders", "frames")
OUT = os.path.join(HERE, "..", "docs", "renders")  # what the concept page loads
os.makedirs(OUT, exist_ok=True)
RES = os.path.join(HERE, "..", "resources")  # what the plugin embeds
os.makedirs(RES, exist_ok=True)
W, H = L["size"]

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

plate = Image.open(os.path.join(FR, "plate_front.png")).convert("RGBA")
k = plate.width / W  # 2 at final quality, 1 for previews
meta = {"scale": k, "strips": {}}

for key in ("drive", "small", "console", "trim", "toggle", "jewel"):
    fs = frames(key)
    if not fs: continue
    w, h = fs[0].size
    cols = min(len(fs), 8); rows = (len(fs) + cols - 1) // cols
    sheet = Image.new("RGBA", (w * cols, h * rows))
    for i, f in enumerate(fs): sheet.paste(f, ((i % cols) * w, (i // cols) * h))
    sheet.save(os.path.join(OUT, f"{key}.webp"), "WEBP", quality=90, method=6)
    sheet.save(os.path.join(RES, f"{key}.png"), optimize=True)  # lossless, for the plugin binary
    meta["strips"][key] = {"w": w / k, "h": h / k, "n": len(fs), "cols": cols, "rows": rows}

for name in ("plate_front", "plate_rear"):
    p = os.path.join(FR, name + ".png")
    if os.path.exists(p):
        Image.open(p).convert("RGB").save(os.path.join(OUT, name + ".jpg"), "JPEG", quality=92, subsampling=0, optimize=True)
        Image.open(p).convert("RGB").save(os.path.join(RES, name + ".jpg"), "JPEG", quality=94, subsampling=0, optimize=True)

json.dump(meta, open(os.path.join(OUT, "strips.json"), "w"), indent=1)

# composite: the front with every part at a representative setting
F = L["front"]
def put(img, key, idx, cx, cy):
    fs = frames(key)
    if not fs: return
    f = fs[min(idx, len(fs) - 1)]
    img.alpha_composite(f, (int(round(cx * k - f.width / 2)), int(round(cy * k - f.height / 2))))
comp = plate.copy()
put(comp, "console", 3, F["console"]["cx"], F["console"]["cy"])
put(comp, "drive", 27, F["drive"]["cx"], F["drive"]["cy"])
put(comp, "small", 18, F["spread"]["cx"], F["spread"]["cy"])
put(comp, "small", 20, F["output"]["cx"], F["output"]["cy"])
put(comp, "toggle", 0, F["power"]["cx"], F["power"]["cy"])
put(comp, "jewel", 1, F["jewel"]["cx"], F["jewel"]["cy"])
comp.convert("RGB").save(os.path.join(HERE, "renders", "composite_front.png"))
print(json.dumps(meta))
