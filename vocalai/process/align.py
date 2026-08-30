"""Time alignment of a vocal against a guide track - the VocALign half.

A doubled or stacked vocal is never performed with the same timing as the
lead, and the fix is not a fixed offset: the two takes drift apart and back
together word by word.  Dynamic time warping finds that correspondence, and
WSOLA renders the dub onto the guide's timeline while leaving its pitch and
timbre alone.

The warp itself is treated with suspicion.  A raw DTW path is jagged and will
happily stall or sprint through a syllable; what gets rendered is a smoothed,
slope-limited version of it, blended toward the original timing by a strength
control, which is the difference between a natural double and a flammed mess.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from ..analysis.features import Spectrogram, mfcc, onset_envelope, pick_onsets, spectrogram
from ..audio.dsp import smooth
from .psola import wsola_time_warp


@dataclass
class AlignConfig:
    """How tightly, and how gently, to pull a dub onto the guide."""

    strength: float = 1.0             # 0 = leave alone, 1 = fully onto the guide
    max_stretch: float = 2.0          # hard limit on local stretch factor
    smooth_ms: float = 50.0           # smoothing applied to the warp curve
    band_seconds: float = 2.0         # how far apart the takes may drift
    coarse_factor: int = 8            # decimation for the first DTW pass
    onset_weight: float = 2.0         # onsets carry the timing information
    n_mfcc: int = 16
    silence_db: float = -55.0         # below this, a frame carries no timing cue
    preserve_ends: bool = True        # do not stretch leading/trailing silence


@dataclass
class AlignResult:
    """The warp that was applied, plus how much work it had to do."""

    warp: np.ndarray                  # guide frame -> dub frame
    hop: int
    sample_rate: int
    offsets_ms: np.ndarray = field(default_factory=lambda: np.zeros(0))
    stats: dict = field(default_factory=dict)

    def offset_at(self, seconds: float) -> float:
        """Timing correction applied at a given point, in milliseconds."""
        f = seconds * self.sample_rate / self.hop
        idx = np.arange(self.warp.size)
        return float(np.interp(f, idx, (self.warp - idx) * self.hop / self.sample_rate * 1000.0))


def _align_features(x: np.ndarray, sample_rate: int, hop: int, cfg: AlignConfig,
                    spec: Spectrogram | None = None) -> tuple[np.ndarray, np.ndarray]:
    """Frame features for alignment, plus a per-frame loudness in dB."""
    spec = spec or spectrogram(x, sample_rate, hop=hop)
    coeffs = mfcc(spec, n_mfcc=cfg.n_mfcc)
    energy = np.sqrt(np.sum(spec.mag**2, axis=1) + 1e-20)
    db = 20.0 * np.log10(np.maximum(energy / max(spec.mag.shape[1], 1), 1e-10))

    timbre = coeffs[:, 1:]  # drop the loudness coefficient; level should not drive timing
    delta = np.vstack([np.zeros((1, timbre.shape[1])), np.diff(timbre, axis=0)])
    onset = onset_envelope(spec)[:, None]

    feats = np.hstack([timbre, 2.0 * delta, cfg.onset_weight * 30.0 * onset])
    norm = np.linalg.norm(feats, axis=1, keepdims=True)
    feats = np.divide(feats, norm, out=np.zeros_like(feats), where=norm > 1e-9)
    return feats, db


def _cost_rows(guide: np.ndarray, dub: np.ndarray, lo: np.ndarray, width: int) -> np.ndarray:
    """Banded cosine-distance cost: row i covers dub frames [lo[i], lo[i]+width)."""
    n = guide.shape[0]
    m = dub.shape[0]
    cols = lo[:, None] + np.arange(width)[None, :]
    valid = (cols >= 0) & (cols < m)
    cols = np.clip(cols, 0, m - 1)
    sim = np.einsum("ik,ijk->ij", guide, dub[cols])
    cost = 1.0 - sim
    cost[~valid] = np.inf
    return cost


def _banded_dtw(cost: np.ndarray, lo: np.ndarray) -> np.ndarray:
    """DTW over a band with steps {(1,1), (1,2), (2,1)}.

    Forbidding a pure horizontal or vertical step bounds the local slope to
    [1/2, 2] and, conveniently, removes the within-row dependency, so each row
    is one vectorised operation instead of a Python loop over columns.
    """
    n, width = cost.shape
    inf = np.inf
    acc = np.full((n, width), inf)
    back = np.zeros((n, width), dtype=np.uint8)

    acc[0, 0] = cost[0, 0]
    for i in range(1, n):
        # Local column jl of row i is global column lo[i] + jl, so reading the
        # previous row at global j-1 means reading its local jl + shift - 1.
        shift1 = int(lo[i] - lo[i - 1])
        prev1 = _shift(acc[i - 1], 1 - shift1, width)      # (i-1, j-1)
        prev2 = _shift(acc[i - 1], 2 - shift1, width)      # (i-1, j-2)
        if i >= 2:
            shift2 = int(lo[i] - lo[i - 2])
            prev3 = _shift(acc[i - 2], 1 - shift2, width)  # (i-2, j-1)
        else:
            prev3 = np.full(width, inf)
        stack = np.vstack([prev1, prev2, prev3])
        choice = np.argmin(stack, axis=0)
        best = stack[choice, np.arange(width)]
        acc[i] = best + cost[i]
        back[i] = choice.astype(np.uint8)
    return _backtrack(acc, back, lo)


def _shift(row: np.ndarray, k: int, width: int) -> np.ndarray:
    """``row`` re-indexed so that element j reads the source at j - k."""
    out = np.full(width, np.inf)
    if k >= 0:
        if k < width:
            out[k:] = row[: width - k]
    else:
        k = -k
        if k < width:
            out[: width - k] = row[k:]
    return out


def _backtrack(acc: np.ndarray, back: np.ndarray, lo: np.ndarray) -> np.ndarray:
    n, width = acc.shape
    last = acc[n - 1]
    if not np.isfinite(last).any():
        return np.stack([np.arange(n), np.arange(n)], axis=1)
    j = int(np.argmin(last))
    path = []
    i = n - 1
    while i >= 0:
        path.append((i, int(lo[i] + j)))
        if i == 0:
            break
        c = back[i, j]
        if c == 0:
            j = j - 1 + int(lo[i] - lo[i - 1])
            i -= 1
        elif c == 1:
            j = j - 2 + int(lo[i] - lo[i - 1])
            i -= 1
        else:
            j = j - 1 + int(lo[i] - lo[i - 2])
            i -= 2
        j = int(np.clip(j, 0, width - 1))
    return np.array(path[::-1])


def _path_to_warp(path: np.ndarray, n_guide: int) -> np.ndarray:
    """Collapse a DTW path into one dub frame per guide frame."""
    warp = np.full(n_guide, np.nan)
    for i, j in path:
        if 0 <= i < n_guide:
            warp[i] = j if not np.isfinite(warp[i]) else 0.5 * (warp[i] + j)
    idx = np.arange(n_guide)
    ok = np.isfinite(warp)
    if not ok.any():
        return idx.astype(float)
    return np.interp(idx, idx[ok], warp[ok])


def dtw_warp(
    guide_feats: np.ndarray,
    dub_feats: np.ndarray,
    band: int,
    coarse_factor: int = 8,
) -> np.ndarray:
    """Multi-resolution banded DTW; returns dub frame per guide frame."""
    n, m = guide_feats.shape[0], dub_feats.shape[0]
    if n < 4 or m < 4:
        return np.linspace(0, max(m - 1, 0), max(n, 1))

    if coarse_factor > 1 and n > 4 * coarse_factor and m > 4 * coarse_factor:
        g_c = _decimate(guide_feats, coarse_factor)
        d_c = _decimate(dub_feats, coarse_factor)
        coarse = dtw_warp(g_c, d_c, band=max(band // coarse_factor, 8), coarse_factor=1)
        guide_pos = np.linspace(0, n - 1, coarse.size)
        centre = np.interp(np.arange(n), guide_pos, coarse * coarse_factor)
        # The fine pass must be free to disagree with the coarse one, so the
        # band is several coarse frames wide rather than just one.
        radius = max(5 * coarse_factor, 24)
    else:
        centre = np.linspace(0, m - 1, n)
        radius = band

    width = int(min(2 * radius + 1, m))
    lo = np.clip(np.round(centre - width // 2).astype(int), 0, max(m - width, 0))
    lo = np.maximum.accumulate(lo)  # the band must not walk backwards
    cost = _cost_rows(guide_feats, dub_feats, lo, width)
    path = _banded_dtw(cost, lo)
    return _path_to_warp(path, n)


def _decimate(feats: np.ndarray, factor: int) -> np.ndarray:
    n = feats.shape[0] // factor * factor
    if n == 0:
        return feats
    block = feats[:n].reshape(-1, factor, feats.shape[1]).mean(axis=1)
    norm = np.linalg.norm(block, axis=1, keepdims=True)
    return np.divide(block, norm, out=np.zeros_like(block), where=norm > 1e-9)


def _condition_warp(
    warp: np.ndarray, guide_db: np.ndarray, dub_db: np.ndarray, cfg: AlignConfig,
    frame_rate: float, m_frames: int,
) -> np.ndarray:
    """Smooth, slope-limit and blend the raw path into something renderable."""
    n = warp.size
    idx = np.arange(n, dtype=float)

    # Where the guide is silent there is nothing to align to; hold the local
    # offset rather than letting the path wander through the gap.
    quiet = guide_db < cfg.silence_db
    offset = warp - idx * (m_frames / max(n, 1))
    if quiet.any() and (~quiet).any():
        offset[quiet] = np.interp(idx[quiet], idx[~quiet], offset[~quiet])
    warp = offset + idx * (m_frames / max(n, 1))

    width = max(1, int(cfg.smooth_ms * frame_rate / 1000.0))
    warp = np.nan_to_num(smooth(warp, width), nan=0.0)

    # Enforce monotonicity and the stretch limit, in one forward pass.
    lo_step = 1.0 / cfg.max_stretch
    hi_step = cfg.max_stretch
    out = np.empty(n)
    out[0] = warp[0]
    for i in range(1, n):
        step = np.clip(warp[i] - warp[i - 1], lo_step, hi_step)
        out[i] = out[i - 1] + step

    if cfg.strength < 1.0:
        neutral = idx * (m_frames / max(n, 1))
        out = neutral + cfg.strength * (out - neutral)
    return np.clip(out, 0, max(m_frames - 1, 0))


def align_to_guide(
    dub: np.ndarray,
    guide: np.ndarray,
    sample_rate: int,
    hop: int = 256,
    cfg: AlignConfig | None = None,
    dub_spec: Spectrogram | None = None,
    guide_spec: Spectrogram | None = None,
) -> tuple[np.ndarray, AlignResult]:
    """Warp ``dub`` onto the timing of ``guide``.

    Returns the rendered audio (guide length) and the warp that produced it.
    """
    cfg = cfg or AlignConfig()
    frame_rate = sample_rate / hop

    g_feats, g_db = _align_features(guide, sample_rate, hop, cfg, guide_spec)
    d_feats, d_db = _align_features(dub, sample_rate, hop, cfg, dub_spec)

    band = max(int(cfg.band_seconds * frame_rate), 16)
    warp = dtw_warp(g_feats, d_feats, band=band, coarse_factor=cfg.coarse_factor)
    warp = _condition_warp(warp, g_db, d_db, cfg, frame_rate, d_feats.shape[0])

    out_len = guide.size
    out_frames = np.arange(out_len) / hop
    time_map = np.interp(out_frames, np.arange(warp.size), warp) * hop
    time_map = np.clip(time_map, 0, max(dub.size - 1, 0))

    d_spec = dub_spec or spectrogram(dub, sample_rate, hop=hop)
    transients = pick_onsets(onset_envelope(d_spec), d_spec.times, delta=0.05) * sample_rate
    rendered = wsola_time_warp(dub, sample_rate, time_map, transients=transients)

    idx = np.arange(warp.size)
    shift_ms = (warp - idx) * hop / sample_rate * 1000.0
    audible = g_db >= cfg.silence_db
    measured = shift_ms[audible] if audible.any() else shift_ms
    stats = {
        "mean_shift_ms": round(float(np.mean(measured)), 2),
        "max_shift_ms": round(float(np.max(np.abs(measured))) if measured.size else 0.0, 2),
        "median_abs_shift_ms": round(float(np.median(np.abs(measured))) if measured.size else 0.0, 2),
        "max_stretch": round(float(np.max(np.abs(np.diff(warp)))) if warp.size > 1 else 1.0, 3),
    }
    return rendered, AlignResult(
        warp=warp, hop=hop, sample_rate=sample_rate, offsets_ms=shift_ms, stats=stats
    )
