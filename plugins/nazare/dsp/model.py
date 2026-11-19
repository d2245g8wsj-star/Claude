"""
Nazaré reference model: the signal path of all six consoles, in Python.

This is the specification the JUCE implementation follows, sample for sample
where it matters. One chain, six parameter sets (models.json, fitted by
fit.py against measured targets):

  in → HPF → pre-shelves → [4× up] shaper [4× down] → inverse shelves
     → tone EQ → output gain → crosstalk (BUS) → gated noise → out

The shaper is memoryless, as measured on the reference: an |x| term (a
fixed even colour that does not grow with level), x² and x³ terms (colour
that does), an optional biased tanh (the valve), and a soft clip. Pre- and
post-shelves around it make the distortion depend on frequency without
changing the frequency response: lows hit a transformer harder, highs hit a
slew-limited stage harder.

Three things are deliberately not on the reference and are Nazaré's own:
  - oversampling around the shaper (the reference aliases up to -27 dB),
  - per-channel component spread (the reference is bit-identical L/R),
  - transient memory: the even colour swells for ~100 ms after a transient
    and settles back to the calibrated value, so every static target holds.

Drive is in dB, 6 = the calibration point. In 'coef' models drive scales the
colour terms (1 dB more drive = 1 dB more THD, clip point fixed), with a
small level bump; in 'pregain' models (the valve) it is real gain into the
stage with partial make-up.
"""
import json, math, os
import numpy as np
from scipy import signal

FS = 48000
OS = 4
# anti-imaging / anti-alias filter for the 4x stage: flat to 21 kHz, -128 dB at 24 kHz
OS_FIR = signal.firwin(1023, 22800, fs=FS * OS, window=("kaiser", 10.0))
HERE = os.path.dirname(os.path.abspath(__file__))


# --------------------------------------------------------------------------- filters
def biquad(kind, f, fs=FS, g_db=0.0, q=0.7071):
    """RBJ cookbook biquad as (b, a)."""
    A = 10 ** (g_db / 40); w = 2 * math.pi * f / fs; c, s = math.cos(w), math.sin(w); al = s / (2 * q)
    if kind == "hp":
        b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]; a = [1 + al, -2 * c, 1 - al]
    elif kind == "hp1":
        k = math.tan(w / 2); b = [1, -1, 0]; a = [1 + k, k - 1, 0]
    elif kind == "peak":
        b = [1 + al * A, -2 * c, 1 - al * A]; a = [1 + al / A, -2 * c, 1 - al / A]
    elif kind == "ls":
        sq = 2 * math.sqrt(A) * al
        b = [A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq)]
        a = [(A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq]
    elif kind == "hs":
        sq = 2 * math.sqrt(A) * al
        b = [A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq)]
        a = [(A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq]
    elif kind == "lp1":
        k = math.tan(w / 2); b = [k, k, 0]; a = [1 + k, k - 1, 0]
    else:
        raise ValueError(kind)
    b, a = np.array(b) / a[0], np.array(a) / a[0]
    return b, a

def response(filters, f, fs=FS):
    z1 = np.exp(-2j * np.pi * np.asarray(np.atleast_1d(f), float) / fs); z2 = z1 * z1
    h = np.ones_like(z1)
    for b, a in filters:
        h *= (b[0] + b[1] * z1 + b[2] * z2) / (a[0] + a[1] * z1 + a[2] * z2)
    return h

def pre_filters(P):
    out = []
    if P["pre"].get("lf_g"): out.append(biquad("ls", P["pre"]["lf_f"], g_db=P["pre"]["lf_g"], q=0.6))
    if P["pre"].get("hf_g"): out.append(biquad("hs", P["pre"]["hf_f"], g_db=P["pre"]["hf_g"], q=0.6))
    return out

def post_filters(P):
    return [(a, b) for b, a in pre_filters(P)]  # exact inverse: shelves are minimum phase

def eq_filters(P):
    e, out = P["eq"], []
    if e.get("hp_f"): out.append(biquad("hp" if e.get("hp_order", 1) == 2 else "hp1", e["hp_f"], q=e.get("hp_q", 0.7071)))
    if e.get("ls_g"): out.append(biquad("ls", e["ls_f"], g_db=e["ls_g"], q=0.6))
    if e.get("pk_g"): out.append(biquad("peak", e["pk_f"], g_db=e["pk_g"], q=e.get("pk_q", 0.8)))
    if e.get("hs_g"): out.append(biquad("hs", e["hs_f"], g_db=e["hs_g"], q=0.6))
    return out


# --------------------------------------------------------------------------- the stage
def drive_scale(P, drive_db):
    return 10 ** ((drive_db - 6.0) / 20)

def shaper(u, P, drive_db=6.0, k1_mod=None, spread=None):
    """Memoryless transfer, in full-scale units. u: numpy array."""
    s = P["shaper"]; D = drive_scale(P, drive_db)
    k1, a2, a3 = s["k1"], s["a2"], s["a3"]
    clip_c = s["clip_c"]
    if spread is not None:
        k1 *= spread["k1"]; a2 *= spread["k1"]; clip_c *= spread["clip"]
    bias_scale = 1.0
    if P["drive_mode"] == "coef":
        k1, a2, a3, G = k1 * D, a2 * D, a3 * D, 1.0
    else:
        # above drive 0 the valve gets real input gain; below the calibrated range its colour
        # terms and bias shrink instead, so the quiet zones (PRISTINE..SILK) stay reachable
        bias_scale = 10 ** (min(drive_db, 0.0) / 20)
        G_base = 10 ** ((max(drive_db, 0.0) - 6.0) / 20) * s.get("pre_gain", 1.0)
        G = G_base * bias_scale
        k1, a2, a3 = k1 * bias_scale, a2 * bias_scale, a3 * bias_scale
    x = u * G
    m = 1.0 if k1_mod is None else k1_mod  # transient memory swells the even terms and the valve's bias
    v = x + k1 * m * np.abs(x) + a2 * m * x * x + a3 * x * x * x
    xo = s.get("xover", 0.0)
    if xo:  # crossover: a fixed absolute error around zero, so it is relatively worse on quiet signals
        eps = 1e-3
        v = v + xo * eps * np.tanh(x / eps)
    g, b = s.get("tube_g", 0.0), s.get("tube_b", 0.0)
    if g > 0:
        t0 = math.tanh(g * b); slope = g * (1 - t0 * t0)
        bb = b * m * bias_scale
        v = (np.tanh(g * (v + bb)) - np.tanh(g * bb)) / slope
    p = s["clip_p"]
    v = v / (1 + (np.abs(v) / clip_c) ** p) ** (1 / p)
    if P["drive_mode"] != "coef":
        v = v / (G_base ** s.get("makeup", 1.0) * bias_scale)
    return v

def out_gain_db(P, drive_db):
    g = P["gain_db"]
    if P["drive_mode"] == "coef":
        g += P.get("bump", 0.0) * (10 ** (min(drive_db, 24.0) / 20) - 10 ** (6 / 20))
    return g


# --------------------------------------------------------------------------- the whole channel pair
def spread_for(channel, amount):
    """Per-channel component values, fixed per channel number (1-48)."""
    # golden-ratio sequence: neighbouring channels land far apart, every channel is reproducible
    r = np.array([((int(channel) * 0.6180339887 + o) % 1.0) * 2 - 1 for o in (0.13, 0.47, 0.81)])
    r = np.array([r[0], ((int(channel) * 0.7548776662 + 0.29) % 1.0) * 2 - 1, ((int(channel) * 0.5698402910 + 0.61) % 1.0) * 2 - 1])
    return {"gain_db": 0.15 * amount * r[0], "k1": 10 ** (2.0 * amount * r[1] / 20), "clip": 1 + 0.04 * amount * r[2]}

def process(x, P, drive_db=6.0, mode="channel", spread=0.0, channels=(7, 8), memory=None, noise=True, oversample=OS, seed=3):
    """x: (2, n) float. Returns (2, n). spread 0..1, memory None = model default."""
    x = np.asarray(x, np.float64)
    out = np.empty_like(x)
    mem = P.get("memory", 0.3) if memory is None else memory
    for ch in (0, 1):
        sp = spread_for(channels[ch], spread) if spread > 0 else None
        y = x[ch]
        for b, a in pre_filters(P):
            y = signal.lfilter(b, a, y)
        k1_mod = None
        if mem > 0:
            env = np.abs(y)
            fast = signal.lfilter([1 - math.exp(-1 / (0.004 * FS))], [1, -math.exp(-1 / (0.004 * FS))], env)
            slow = signal.lfilter([1 - math.exp(-1 / (0.15 * FS))], [1, -math.exp(-1 / (0.15 * FS))], env)
            k1_mod = 1 + mem * np.tanh(np.maximum(fast - slow, 0) / 0.25)
        if oversample > 1:
            u = signal.resample_poly(y, oversample, 1, window=OS_FIR)
            km = np.repeat(k1_mod, oversample)[: len(u)] if k1_mod is not None else None
            v = shaper(u, P, drive_db, km, sp)
            y = signal.resample_poly(v, 1, oversample, window=OS_FIR)[: x.shape[1]]
        else:
            y = shaper(y, P, drive_db, k1_mod, sp)
        for b, a in post_filters(P) + eq_filters(P) + [biquad("hp1", P["eq"].get("dc_f", 3.0))]:
            y = signal.lfilter(b, a, y)
        g = out_gain_db(P, drive_db) + (sp["gain_db"] if sp else 0.0)
        out[ch] = y * 10 ** (g / 20)
    if mode == "bus":
        xt = P["xtalk"]
        def leak(y):
            lo = signal.lfilter(*biquad("lp1", xt["lf_f"]), y); hi = signal.lfilter(*biquad("hp1", xt["hf_f"]), y)
            return xt["flat"] * y + xt["lf"] * lo + xt["hf"] * hi
        l, r = out[0].copy(), out[1].copy()
        out[0] += leak(r); out[1] += leak(l)
    if noise and P.get("noise_dbfs") is not None:
        rng = np.random.default_rng(seed)
        n = rng.standard_normal(out.shape) * 10 ** (P["noise_dbfs"] / 20)
        n = signal.lfilter(*biquad("lp1", 16000), n)
        env = np.abs(x).max(axis=0)
        a_ = math.exp(-1 / (0.05 * FS))
        held = signal.lfilter([1 - a_], [1, -a_], (env > 10 ** (-80 / 20)).astype(float))
        out += n * np.clip(held * 4, 0, 1)
    return out


def load(path=os.path.join(HERE, "models.json")):
    return json.load(open(path))
