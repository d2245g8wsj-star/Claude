"""
Nazaré reference measurements: the test signal and everything we read off a
plugin's output. Black box only: signal in, signal out.

The signal is one long stereo file made of segments (stepped sines, noise,
two-tone IMD, one-sided tones for crosstalk, silence, a level step, an
impulse). The manifest says where each segment sits, so the same analysis
runs on output rendered by run_plugins.py or exported from a DAW.

Levels are dBFS peak (AES17): a full-scale sine is 0 dBFS. The console
nominal level, 0 VU, is taken as -18 dBFS.
"""
import json, math
import numpy as np

FS = 48000
NOMINAL = -18.0


# --------------------------------------------------------------------------- signal
def build_signal(fs=FS, seed=7):
    rng = np.random.default_rng(seed)
    segs, parts, t0 = [], [], 0

    def add(kind, left, right=None, **meta):
        nonlocal t0
        right = left if right is None else right
        n = len(left)
        parts.append(np.stack([left, right]).astype(np.float32))
        segs.append(dict(kind=kind, start=t0, length=n, **meta))
        t0 += n
        gap = np.zeros((2, int(0.05 * fs)), np.float32)
        parts.append(gap); t0 += gap.shape[1]

    def tone(f, level, dur):
        t = np.arange(int(dur * fs)) / fs
        x = 10 ** (level / 20) * np.sin(2 * np.pi * f * t)
        ramp = int(0.005 * fs); x[:ramp] *= np.linspace(0, 1, ramp)
        return x

    add("impulse", np.r_[np.zeros(int(0.02 * fs)), [0.5], np.zeros(int(0.48 * fs))])
    add("silence", np.zeros(int(3 * fs)))
    for f in (100.0, 1000.0, 10000.0):
        for level in range(-48, 7, 3):
            add("sine", tone(f, level, 1.0), freq=f, level=float(level))
    for level in (-30.0, -18.0, -6.0):
        t = np.arange(int(1.0 * fs)) / fs
        a = 10 ** (level / 20) / 1.25
        x = a * np.sin(2 * np.pi * 60 * t) + 0.25 * a * np.sin(2 * np.pi * 7000 * t)
        add("imd", x, level=level)
    for side in ("L", "R"):
        for f in (100.0, 1000.0, 10000.0):
            x, z = tone(f, NOMINAL, 1.0), np.zeros(int(fs))
            add("xtalk", x if side == "L" else z, z if side == "L" else x, side=side, freq=f)
    for level in (-30.0, -18.0, -6.0):
        n = rng.standard_normal(int(6 * fs)); n *= 10 ** (level / 20) / np.sqrt(np.mean(n ** 2))
        add("noise", n, level=level)
    step = np.r_[tone(1000, -24, 1.0), tone(1000, 0, 1.0)[: int(fs)], tone(1000, -24, 2.0)]
    add("step", step, freq=1000.0, levels=[-24.0, 0.0, -24.0], times=[1.0, 1.0, 2.0])
    sig = np.concatenate(parts, axis=1)
    return sig, {"fs": fs, "nominal_dbfs": NOMINAL, "segments": segs}


# --------------------------------------------------------------------------- helpers
def _bh(n):
    k = np.arange(n) / n
    return 0.35875 - 0.48829 * np.cos(2 * np.pi * k) + 0.14128 * np.cos(4 * np.pi * k) - 0.01168 * np.cos(6 * np.pi * k)

def _band_power(spec, f, df, half=4):
    k = int(round(f / df))
    if k + half >= len(spec): return float("nan")
    return float(np.sum(spec[max(1, k - half): k + half + 1]))

def db(x, floor=1e-30): return 10 * math.log10(max(x, floor))

def harmonics(x, f, fs, nh=10):
    """Power of H1..Hn of a steady tone, from a Blackman-Harris spectrum."""
    w = _bh(len(x)); spec = np.abs(np.fft.rfft(x * w)) ** 2; df = fs / len(x)
    return [_band_power(spec, f * h, df) for h in range(1, nh + 1)]

def latency(out, sig, seg):
    a, b = seg["start"], seg["start"] + seg["length"]
    i_in = int(np.argmax(np.abs(sig[0, a:b])))
    win = out[:, a: b + 8192]
    i_out = int(np.argmax(np.abs(win).sum(axis=0)))
    return i_out - i_in

def welch_tf(x, y, fs, n=8192):
    hop, w = n // 2, np.hanning(n)
    sxx = np.zeros(n // 2 + 1); sxy = np.zeros(n // 2 + 1, complex)
    for i in range(0, len(x) - n, hop):
        X = np.fft.rfft(x[i:i + n] * w); Y = np.fft.rfft(y[i:i + n] * w)
        sxx += np.abs(X) ** 2; sxy += np.conj(X) * Y
    return np.fft.rfftfreq(n, 1 / fs), sxy / np.maximum(sxx, 1e-30)

THIRD_OCT = [20 * 2 ** (i / 6) for i in range(61)]  # 20 Hz .. 20.5 kHz in 1/6 octaves

def _at(freqs, h, fc):
    sel = (freqs > fc * 2 ** (-1 / 12)) & (freqs < fc * 2 ** (1 / 12))
    return complex(np.mean(h[sel])) if np.any(sel) else complex("nan")


# --------------------------------------------------------------------------- analysis
def analyse(out, sig, manifest):
    fs = manifest["fs"]; segs = manifest["segments"]
    lat = latency(out, sig, next(s for s in segs if s["kind"] == "impulse"))
    o = np.roll(out, -lat, axis=1) if lat > 0 else out
    def cut(s, skip=0.0, keep=None):
        a = s["start"] + int(skip * fs)
        b = s["start"] + s["length"] if keep is None else a + int(keep * fs)
        return o[:, a:b], sig[:, a:b]
    R = {"latency_samples": int(lat), "fs": fs}

    # noise floor
    s = next(s for s in segs if s["kind"] == "silence"); y, _ = cut(s, 0.5)
    R["noise_floor_dbfs_rms"] = [db(float(np.mean(ch.astype(np.float64) ** 2))) for ch in y]
    f, _ = np.fft.rfftfreq(8192, 1 / fs), None
    ps = np.mean([np.abs(np.fft.rfft(y[0, i:i + 8192] * np.hanning(8192))) ** 2 for i in range(0, y.shape[1] - 8192, 4096)], axis=0)
    hum = {str(hz): db(_band_power(ps, hz, fs / 8192, 2)) - db(float(np.median(ps))) for hz in (50, 60, 100, 120, 150, 180)}
    R["noise_hum_above_median_db"] = hum

    # stepped sines: gain, THD, harmonics per channel
    sweep = []
    for s in (s for s in segs if s["kind"] == "sine"):
        y, x = cut(s, 0.5, 0.5)
        hin = harmonics(x[0].astype(np.float64), s["freq"], fs)
        row = {"freq": s["freq"], "level": s["level"], "ch": []}
        for ch in (0, 1):
            h = harmonics(y[ch].astype(np.float64), s["freq"], fs)
            valid = [p for i, p in enumerate(h[1:], start=2) if not math.isnan(p) and s["freq"] * i < 0.45 * fs]
            thd = math.sqrt(sum(valid) / h[0]) if h[0] > 0 else float("nan")
            row["ch"].append({
                "gain_db": db(h[0]) - db(hin[0]),
                "thd_pct": 100 * thd,
                "h_dbc": [None if math.isnan(p) or s["freq"] * (i + 2) >= 0.45 * fs else round(db(p) - db(h[0]), 2) for i, p in enumerate(h[1:])],
            })
        sweep.append(row)
    R["sines"] = sweep

    # SMPTE IMD (60 Hz + 7 kHz, 4:1): sidebands around 7 kHz
    imd = []
    for s in (s for s in segs if s["kind"] == "imd"):
        y, _ = cut(s, 0.3, 0.6)
        w = _bh(y.shape[1]); spec = np.abs(np.fft.rfft(y[0].astype(np.float64) * w)) ** 2; df = fs / y.shape[1]
        carrier = _band_power(spec, 7000, df)
        side = sum(_band_power(spec, 7000 + k * 60, df, 2) for k in (-3, -2, -1, 1, 2, 3))
        imd.append({"level": s["level"], "imd_pct": 100 * math.sqrt(side / carrier)})
    R["imd_smpte"] = imd

    # crosstalk: tone on one side, level that appears on the other
    xt = []
    for s in (s for s in segs if s["kind"] == "xtalk"):
        y, x = cut(s, 0.3, 0.6)
        src, dst = (0, 1) if s["side"] == "L" else (1, 0)
        p_src = harmonics(y[src].astype(np.float64), s["freq"], fs, 1)[0]
        p_dst = harmonics(y[dst].astype(np.float64), s["freq"], fs, 1)[0]
        xt.append({"from": s["side"], "freq": s["freq"], "xtalk_db": db(p_dst) - db(p_src)})
    R["crosstalk"] = xt

    # frequency response at three levels, and the left/right mismatch
    fr = []
    for s in (s for s in segs if s["kind"] == "noise"):
        y, x = cut(s, 0.5)
        entry = {"level": s["level"], "f": [round(fc, 1) for fc in THIRD_OCT], "ch": []}
        hs = []
        for ch in (0, 1):
            fq, h = welch_tf(x[ch].astype(np.float64), y[ch].astype(np.float64), fs)
            hs.append(h)
            pts = [_at(fq, h, fc) for fc in THIRD_OCT]
            ref = abs(_at(fq, h, 1000.0))
            entry["ch"].append({"mag_db": [round(20 * math.log10(max(abs(p), 1e-12) / ref), 3) for p in pts],
                                "phase_deg": [round(math.degrees(np.angle(p)), 2) for p in pts]})
        d = [_at(fq, hs[0] / hs[1], fc) for fc in THIRD_OCT]
        entry["lr_diff_db"] = [round(20 * math.log10(max(abs(p), 1e-12)), 3) for p in d]
        entry["lr_diff_deg"] = [round(math.degrees(np.angle(p)), 3) for p in d]
        entry["gain_1k_db"] = 20 * math.log10(max(abs(_at(fq, hs[0], 1000.0)), 1e-12))
        fr.append(entry)
    R["response"] = fr

    # level step: how gain and H2/H3 move over time (dynamic bias, memory)
    s = next(s for s in segs if s["kind"] == "step"); y, x = cut(s)
    win, hop = int(0.02 * fs), int(0.01 * fs); track = []
    for i in range(0, y.shape[1] - win, hop):
        hy = harmonics(y[0, i:i + win].astype(np.float64), 1000.0, fs, 3)
        hx = harmonics(x[0, i:i + win].astype(np.float64), 1000.0, fs, 1)
        track.append({"t": round((i + win / 2) / fs, 3), "gain_db": round(db(hy[0]) - db(hx[0]), 3),
                      "h2_dbc": round(db(hy[1]) - db(hy[0]), 2), "h3_dbc": round(db(hy[2]) - db(hy[0]), 2)})
    R["step"] = track
    return R


def summary(R):
    """A few headline numbers per run, for a table."""
    s1k = [r for r in R["sines"] if r["freq"] == 1000.0]
    at = lambda lv: next((r for r in s1k if r["level"] == lv), None)
    nom = at(-18.0); hot = at(0.0)
    return {
        "latency": R["latency_samples"],
        "noise_dbfs": round(max(R["noise_floor_dbfs_rms"]), 1),
        "thd_1k_nominal_pct": round(nom["ch"][0]["thd_pct"], 5) if nom else None,
        "h2_1k_nominal_dbc": nom["ch"][0]["h_dbc"][0] if nom else None,
        "h3_1k_nominal_dbc": nom["ch"][0]["h_dbc"][1] if nom else None,
        "thd_1k_0dbfs_pct": round(hot["ch"][0]["thd_pct"], 4) if hot else None,
        "gain_1k_nominal_db": round(nom["ch"][0]["gain_db"], 3) if nom else None,
        "xtalk_1k_db": round(next(x["xtalk_db"] for x in R["crosstalk"] if x["freq"] == 1000.0 and x["from"] == "L"), 1),
        "xtalk_10k_db": round(next(x["xtalk_db"] for x in R["crosstalk"] if x["freq"] == 10000.0 and x["from"] == "L"), 1),
        "lr_mismatch_1k_db": round(R["response"][1]["lr_diff_db"][THIRD_OCT.index(min(THIRD_OCT, key=lambda f: abs(f - 1000)))], 3),
    }


def write_wav(path, data, fs=FS):
    """32-bit float WAV, no dependencies."""
    import struct
    data = np.asarray(data, np.float32); ch, n = data.shape
    inter = data.T.tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 4 + 26 + 12 + 8 + len(inter)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHHH", 18, 3, ch, fs, fs * ch * 4, ch * 4, 32, 0))
        f.write(b"fact" + struct.pack("<II", 4, n))
        f.write(b"data" + struct.pack("<I", len(inter)) + inter)

def read_wav(path):
    """Reads 16/24/32-bit PCM or 32-bit float WAV into (channels, samples) float64."""
    import struct
    raw = open(path, "rb").read(); i = 12; fmt = None
    while i < len(raw):
        cid, size = raw[i:i + 4], struct.unpack("<I", raw[i + 4:i + 8])[0]; body = raw[i + 8:i + 8 + size]
        if cid == b"fmt ": tag, ch, fs, _, _, bits = struct.unpack("<HHIIHH", body[:16]); fmt = (tag, ch, fs, bits)
        if cid == b"fmt " and tag == 0xFFFE: fmt = (struct.unpack("<H", body[24:26])[0], ch, fs, bits)
        if cid == b"data": data = body
        i += 8 + size + (size & 1)
    tag, ch, fs, bits = fmt
    if tag == 3 and bits == 32: x = np.frombuffer(data, "<f4").astype(np.float64)
    elif tag == 3 and bits == 64: x = np.frombuffer(data, "<f8")
    elif bits == 16: x = np.frombuffer(data, "<i2") / 32768.0
    elif bits == 24:
        b = np.frombuffer(data, np.uint8).reshape(-1, 3)
        v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(v >= 1 << 23, v - (1 << 24), v) / float(1 << 23)
    elif bits == 32: x = np.frombuffer(data, "<i4") / 2147483648.0
    else: raise ValueError(f"unsupported WAV format {fmt}")
    return x.reshape(-1, ch).T, fs
