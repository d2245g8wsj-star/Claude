"""
Fits the six Nazaré consoles to their calibration targets and writes
models.json. Targets come from black-box measurements of a reference
console plugin (measure/reference/vcc/summary.csv, Channel, three drive
settings) mapped to each Nazaré model by character; GLASS is Nazaré's own,
derived from the cleanest reference console with 14 dB less distortion.

    python fit.py

Steady-state sines through a memoryless shaper between linear filters can be
computed exactly from one period, so the fit is fast: per frequency, the
pre-filter sets the level into the shaper, and each harmonic h is then
weighted by the post-filters at h·f.
"""
import csv, json, math, os
import numpy as np
from scipy.optimize import least_squares
import model as M

HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.join(HERE, "..", "measure", "reference", "vcc", "summary.csv")
FS = M.FS
FREQS, LEVELS, DRIVES = (100.0, 1000.0, 10000.0), (-18, -6, 0), (0.0, 6.0, 18.0)
FR_F = (20, 50, 100, 200, 2000, 5000, 10000, 16000, 20000)
FLOOR = -130.0

# Nazaré model -> reference console (by character), plus the reference's THD at +3 dBFS, drive 6 (report table 4.1)
MAP = {
    "pipeline":  dict(ref=("Channel", 0), thd_p3=-27, desc="British VCA"),
    "mavericks": dict(ref=("Channel", 2), thd_p3=-17, desc="American discrete"),
    "jaws":      dict(ref=("Channel", 3), thd_p3=-80, desc="British class A"),
    "teahupoo":  dict(ref=("Channel", 4), thd_p3=-69, desc="Germanium"),
    "supertubes": dict(ref=("Channel", 5), thd_p3=-23, desc="British valve"),
    "glass":     dict(ref=("MixBuss", 2), thd_p3=-33, desc="Modern transformerless", cleaner=14.0, fr_scale=0.5),
}
# BUS crosstalk targets in dB at 100 Hz / 1 kHz / 10 kHz (report §6)
XT = {"pipeline": (-76, -75, -77), "mavericks": (-95, -120, -101), "jaws": (-65, -70, -75),
      "teahupoo": (-85, -84, -71), "supertubes": (-41, -50, -42), "glass": (-90, -92, -90)}
NOISE = {"glass": -107, "pipeline": -98, "mavericks": -97, "jaws": -95, "teahupoo": -93, "supertubes": -93}
MEMORY = {"glass": 0.1, "pipeline": 0.25, "mavericks": 0.25, "jaws": 0.35, "teahupoo": 0.3, "supertubes": 0.5}
NAMES = {"glass": "GLASS", "pipeline": "PIPELINE", "mavericks": "MAVERICKS", "jaws": "JAWS", "teahupoo": "TEAHUPO’O", "supertubes": "SUPERTUBES"}


def targets(key):
    rows = list(csv.DictReader(open(REF)))
    plug, con = MAP[key]["ref"]
    T = {}
    for r in rows:
        if r["plugin"] != plug or int(r["console"]) != con: continue
        d = float(r["drive_db"])
        if d not in DRIVES: continue
        g = {k: float(v) for k, v in r.items() if k not in ("plugin", "console")}
        T[d] = g
    c = MAP[key].get("cleaner", 0.0)
    if c:
        for d in T:
            for k in T[d]:
                if k.startswith(("thd_", "H", "imd_")): T[d][k] -= c
            for k in T[d]:
                if k.startswith("fr"): T[d][k] *= MAP[key].get("fr_scale", 1.0)
    T["thd_p3"] = MAP[key]["thd_p3"]
    return T


# --------------------------------------------------------------------------- fast steady-state evaluation
N = 1024
PH = np.sin(2 * np.pi * np.arange(N) / N)

def harmonic_out(P, f, level, drive):
    A = 10 ** (level / 20)
    hp = abs(M.response(M.pre_filters(P), [f])[0]) if M.pre_filters(P) else 1.0
    y = M.shaper(A * hp * PH, P, drive)
    c = np.abs(np.fft.rfft(y)[1:11]) * 2 / N
    hs = np.arange(1, 11) * f
    ok = hs < 0.45 * FS
    post = M.post_filters(P) + M.eq_filters(P) + [M.biquad("hp1", P["eq"].get("dc_f", 3.0))]
    w = np.abs(M.response(post, np.where(ok, hs, 1000.0)))
    g = 10 ** (M.out_gain_db(P, drive) / 20)
    return c * w * g, ok, A

def metrics(P, f, level, drive):
    out, ok, A = harmonic_out(P, f, level, drive)
    p = out[1:][ok[1:]]
    thd = 20 * math.log10(max(math.sqrt(float(np.sum(p ** 2))) / out[0], 1e-9))
    hdb = [20 * math.log10(max(out[h] / out[0], 1e-9)) if ok[h] else None for h in range(1, 10)]
    return {"thd": thd, "gain": 20 * math.log10(out[0] / A), "h": hdb}

def fr(P, f):
    filt = M.eq_filters(P) + [M.biquad("hp1", P["eq"].get("dc_f", 3.0))]
    h = np.abs(M.response(filt, list(f) + [1000.0]))
    return 20 * np.log10(h[:-1] / h[-1])


# --------------------------------------------------------------------------- the fit
def build(key, v, eq, kind):
    P = {"name": NAMES[key], "desc": MAP[key]["desc"], "drive_mode": "pregain" if kind == "tube" else "coef",
         "eq": eq, "gain_db": float(v[9]), "bump": float(v[10]) if kind != "tube" else 0.0,
         "pre": {"lf_f": float(v[5]), "lf_g": float(v[6]), "hf_f": float(v[7]), "hf_g": float(v[8])},
         "shaper": {"k1": float(v[0]), "a2": float(v[1]), "a3": float(v[2]), "clip_c": float(v[3]), "clip_p": float(v[4])}}
    P["shaper"]["xover"] = float(v[11])
    if kind == "tube":
        P["shaper"].update(tube_g=float(v[12]), tube_b=float(v[13]), pre_gain=float(v[14]), makeup=float(v[15]))
    return P

# GLASS is Nazaré's own: the cleanest stage, odd only, a soft ceiling just above full scale
GLASS_SHAPER = {"k1": 0.0, "a2": 2e-5, "a3": -4e-4, "clip_c": 1.25, "clip_p": 6.0}

def residuals(v, key, eq, kind, T):
    P = build(key, v, eq, kind); r = []
    cl = lambda x: max(x, FLOOR)
    for d in DRIVES:
        w = 1.5 if d == 6 else 1.0
        for f in FREQS:
            for L in LEVELS:
                m = metrics(P, f, L, d)
                r.append(w * (cl(m["thd"]) - cl(T[d][f"thd_{int(f)}_{L}"])))
        r.append(4.0 * (metrics(P, 1000, -18, d)["gain"] - T[d]["gain_1k_m18"]))
    m6 = metrics(P, 1000, -6, 6.0)
    for i, h in enumerate(("H2", "H3", "H4", "H5")):
        r.append(0.7 * (cl(m6["h"][i]) - cl(T[6.0][f"{h}_1k_m6"])))
    r.append(4.0 * (metrics(P, 1000, 0, 6.0)["gain"] - T[6.0]["gain_1k_0"]))
    r.append(3.0 * (metrics(P, 1000, 3, 6.0)["gain"] - T[6.0]["gain_1k_p3"]))
    r.append(0.6 * (cl(metrics(P, 1000, 3, 6.0)["thd"]) - T["thd_p3"]))
    return np.array(r)

def fit_eq(T):
    tgt = np.array([T[6.0][f"fr{f}"] for f in FR_F])
    def mk(v):
        return {"hp_f": v[0], "hp_order": 1, "ls_f": v[1], "ls_g": v[2], "pk_f": v[3], "pk_g": v[4], "pk_q": v[5], "hs_f": v[6], "hs_g": v[7], "dc_f": 3.0}
    def res(v):
        P = {"eq": mk(v)}
        return fr(P, FR_F) - tgt
    best = None
    for x0 in ([8, 60, 0.5, 300, -0.2, 0.8, 8000, 0.2], [15, 40, 3.0, 150, -0.3, 0.7, 6000, -0.3], [3, 100, -0.5, 2000, 0.2, 0.6, 12000, 0.3]):
        r = least_squares(res, x0, bounds=([1, 20, -8, 80, -3, 0.3, 2000, -4], [40, 400, 8, 12000, 3, 3, 20000, 4]))
        if best is None or r.cost < best.cost: best = r
    return mk(best.x), float(np.max(np.abs(best.fun)))

def fit_xt(key):
    t = np.array(XT[key], float)
    lp = lambda f: abs(M.response([M.biquad("lp1", 300.0)], [f])[0]); hp = lambda f: abs(M.response([M.biquad("hp1", 3000.0)], [f])[0])
    F = (100.0, 1000.0, 10000.0)
    def res(v):
        return np.array([20 * math.log10(max(abs(v[0] + v[1] * M.response([M.biquad("lp1", 300.0)], [f])[0] + v[2] * M.response([M.biquad("hp1", 3000.0)], [f])[0]), 1e-9)) for f in F]) - t
    best = None
    for x0 in ([1e-4, 1e-4, 1e-4], [1e-3, -1e-3, 1e-3], [1e-5, 1e-3, -1e-5]):
        r = least_squares(res, x0)
        if best is None or r.cost < best.cost: best = r
    return {"flat": float(best.x[0]), "lf": float(best.x[1]), "hf": float(best.x[2]), "lf_f": 300.0, "hf_f": 3000.0}

COEF_LO = [-0.05, -0.2, -0.5, 0.6, 1.5, 30, -18, 300, -18, -3, -1, -1e-9]
COEF_HI = [0.05, 0.2, 0.5, 4.0, 40.0, 600, 18, 12000, 18, 3, 1, 1e-9]
TUBE_LO = COEF_LO + [0.2, -1.0, 0.1, 0.0]
TUBE_HI = COEF_HI + [8.0, 1.0, 4.0, 1.0]

def starts(kind):
    shelves = [(150, 0, 3000, 0), (120, 10, 3000, 0), (150, 0, 500, 12), (120, 10, 500, 12), (150, -6, 3000, 0), (150, 0, 1500, -10), (80, 14, 2000, 6), (100, 16, 1000, 16)]
    out = []
    for lf_f, lf_g, hf_f, hf_g in shelves:
        base = [1e-3, 1e-3, 1e-3, 1.2, 8.0, lf_f, lf_g, hf_f, hf_g, 0.3, 0.1, 0.0]
        out.append(base + ([2.0, 0.15, 1.0, 0.9] if kind == "tube" else []))
        if kind == "coef": out.append(base[:11] + [0.04])
        if kind == "tube": out.append([-0.008, -0.1, 0.02, 1.6, 8.0, lf_f, lf_g, hf_f, hf_g, 0.4, 0.0, 0.0, 1.2, -0.2, 0.7, 0.88])
    return out

def fit(key):
    T = targets(key)
    eq, eq_err = fit_eq(T)
    if key == "glass":
        P = {"name": NAMES[key], "desc": MAP[key]["desc"], "drive_mode": "coef", "eq": eq, "gain_db": 0.0, "bump": 0.02,
             "pre": {"lf_f": 150.0, "lf_g": 0.0, "hf_f": 3000.0, "hf_g": 0.0}, "shaper": dict(GLASS_SHAPER)}
        rms = 0.0
    else:
        kind = "tube" if key == "supertubes" else "coef"
        lo, hi = (list(TUBE_LO), list(TUBE_HI)) if kind == "tube" else (list(COEF_LO), list(COEF_HI))
        if key == "teahupoo": lo[11], hi[11] = -0.5, 0.5  # only the germanium stage has crossover
        best = None
        for x0 in starts(kind):
            x0 = np.clip(x0, np.array(lo) + 1e-9, np.array(hi) - 1e-9)
            r = least_squares(residuals, x0, bounds=(lo, hi), args=(key, eq, kind, T), loss="soft_l1", f_scale=3.0, max_nfev=300)
            if best is None or r.cost < best.cost: best = r
        P = build(key, best.x, eq, kind)
        rms = float(np.sqrt(np.mean(best.fun ** 2)))
    P["xtalk"] = fit_xt(key); P["noise_dbfs"] = NOISE[key]; P["memory"] = MEMORY[key]
    P["reference"] = {"plugin": MAP[key]["ref"][0], "console": MAP[key]["ref"][1]} if key != "glass" else {"designed": True}
    print(f"{key:11s} fit rms {rms:5.2f}  eq max err {eq_err:.3f} dB", flush=True)
    return P, rms

if __name__ == "__main__":
    import sys
    path = os.path.join(HERE, "models.json")
    out = json.load(open(path)) if len(sys.argv) > 1 and os.path.exists(path) else {}
    for key in (sys.argv[1:] or ("glass", "pipeline", "mavericks", "jaws", "teahupoo", "supertubes")):
        P, rms = fit(key); P["fit_rms"] = round(rms, 3); out[key] = P
    json.dump(out, open(os.path.join(HERE, "models.json"), "w"), indent=1)
    print("wrote models.json")
