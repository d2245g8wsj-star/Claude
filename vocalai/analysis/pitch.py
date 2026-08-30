"""Probabilistic monophonic pitch tracking (a pYIN-style estimator).

Melodyne's whole model rests on knowing, for every instant, both *what* pitch
was sung and *how sure* we are that anything was sung at all.  This module
delivers that: YIN's cumulative-mean-normalised difference function supplies
per-frame pitch candidates, a threshold prior turns them into probabilities,
and a hidden Markov model decoded with Viterbi picks the trajectory that is
smooth in the way a human larynx actually moves.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from ..audio.dsp import frame_signal, frame_times, hz_to_cents

# Threshold grid for the pYIN candidate prior.  Low thresholds are strict
# (only very periodic frames pass), high thresholds are permissive.
_THRESHOLDS = np.arange(0.01, 1.0, 0.01)
# Beta(2, 18) prior: mass concentrated near the classic YIN threshold of 0.1.
_A, _B = 2.0, 18.0


def _threshold_prior() -> np.ndarray:
    s = _THRESHOLDS
    w = s ** (_A - 1.0) * (1.0 - s) ** (_B - 1.0)
    return w / w.sum()


@dataclass
class PitchTrack:
    """Frame-wise F0 in Hz (NaN where unvoiced) plus its confidence."""

    f0: np.ndarray
    voiced_prob: np.ndarray
    times: np.ndarray
    sample_rate: int
    hop: int
    periodicity: np.ndarray = field(default_factory=lambda: np.zeros(0))

    @property
    def voiced(self) -> np.ndarray:
        return np.isfinite(self.f0)

    def cents(self, ref: float = 10.0) -> np.ndarray:
        return hz_to_cents(np.nan_to_num(self.f0, nan=0.0), ref=ref)

    def voiced_ratio(self) -> float:
        return float(self.voiced.mean()) if self.f0.size else 0.0

    def frame_at(self, seconds: float) -> int:
        return int(np.clip(round(seconds * self.sample_rate / self.hop), 0, self.f0.size - 1))


def cmndf(frames: np.ndarray, max_lag: int) -> np.ndarray:
    """Cumulative mean normalised difference function, one row per frame."""
    n_frames, n = frames.shape
    win = n - max_lag
    if win <= 0:
        raise ValueError("frame length must exceed the maximum lag")

    short = frames[:, :win]
    nfft = int(2 ** np.ceil(np.log2(n + win)))
    corr = np.fft.irfft(
        np.fft.rfft(frames, nfft) * np.conj(np.fft.rfft(short, nfft)), nfft
    )[:, :max_lag]

    power = frames**2
    cum = np.concatenate([np.zeros((n_frames, 1)), np.cumsum(power, axis=1)], axis=1)
    p0 = cum[:, win : win + 1]                       # energy of the fixed window
    lagged = cum[:, np.arange(max_lag) + win] - cum[:, np.arange(max_lag)]
    d = p0 + lagged - 2.0 * corr
    d = np.maximum(d, 0.0)

    running = np.cumsum(d[:, 1:], axis=1)
    denom = running / np.arange(1, max_lag)[None, :]
    out = np.ones_like(d)
    np.divide(d[:, 1:], denom, out=out[:, 1:], where=denom > 1e-12)
    out[:, 1:][denom <= 1e-12] = 1.0
    return out


def _local_minima(row: np.ndarray) -> np.ndarray:
    return np.flatnonzero((row[1:-1] < row[:-2]) & (row[1:-1] <= row[2:])) + 1


def _observations(
    d: np.ndarray,
    sample_rate: int,
    fmin: float,
    fmax: float,
    bins: np.ndarray,
    no_trough_boost: float = 0.05,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[np.ndarray]]:
    """Turn CMNDF rows into a ``(frames, bins)`` voiced-probability matrix.

    Rather than YIN's single hard threshold, pYIN integrates over a prior on
    the threshold.  For a frame, threshold ``s`` selects the first trough with
    ``d' < s``; as ``s`` sweeps upward the selected trough is always a running
    record minimum, so each record trough collects the prior mass of the
    threshold interval that picks it.  That closed form replaces the loop over
    thresholds entirely.

    Also returns, per frame, the sub-sample-refined candidate frequencies, so
    the decoded pitch can later be snapped off the bin grid onto a real trough.
    """
    n_frames = d.shape[0]
    prior_cdf = np.concatenate([[0.0], np.cumsum(_threshold_prior())])
    lag_lo = max(int(np.floor(sample_rate / fmax)), 1)
    lag_hi = min(int(np.ceil(sample_rate / fmin)), d.shape[1] - 2)

    obs = np.zeros((n_frames, bins.size))
    voiced_prob = np.zeros(n_frames)
    periodicity = np.zeros(n_frames)
    candidates: list[np.ndarray] = []
    empty = np.zeros(0)

    def cdf(v: np.ndarray) -> np.ndarray:
        return prior_cdf[np.searchsorted(_THRESHOLDS, v, side="right")]

    for t in range(n_frames):
        row = d[t]
        troughs = _local_minima(row)
        troughs = troughs[(troughs >= lag_lo) & (troughs <= lag_hi)]
        if troughs.size == 0:
            candidates.append(empty)
            continue
        vals = row[troughs]
        refined = _refine_many(row, troughs)
        freqs = sample_rate / np.maximum(refined, 1e-9)
        keep = (freqs >= fmin) & (freqs <= fmax)
        if not keep.any():
            candidates.append(empty)
            continue
        vals, freqs = vals[keep], freqs[keep]
        candidates.append(freqs)

        prefix_min = np.minimum.accumulate(np.concatenate([[np.inf], vals[:-1]]))
        record = vals < prefix_min
        probs = np.zeros(vals.size)
        upper = np.where(np.isfinite(prefix_min), prefix_min, _THRESHOLDS[-1] + 1.0)
        probs[record] = np.maximum(cdf(upper[record]) - cdf(vals[record]), 0.0)

        best = int(np.argmin(vals))
        # Thresholds too strict for any trough leave the frame nearly unvoiced.
        probs[best] += float(cdf(np.array([vals[best]]))[0]) * no_trough_boost

        total = float(probs.sum())
        if total <= 0.0:
            continue
        voiced_prob[t] = min(total, 1.0)
        periodicity[t] = float(1.0 - vals[best])

        cents = 1200.0 * np.log2(freqs / 10.0)
        idx = np.clip(np.searchsorted(bins, cents) - 1, 0, bins.size - 1)
        np.add.at(obs[t], idx, probs)
    return obs, voiced_prob, periodicity, candidates


def _refine_many(row: np.ndarray, idx: np.ndarray) -> np.ndarray:
    """Vectorised parabolic refinement of several minima in one CMNDF row."""
    i = np.clip(idx, 1, row.size - 2)
    a, b, c = row[i - 1], row[i], row[i + 1]
    denom = a - 2.0 * b + c
    shift = np.where(np.abs(denom) > 1e-12, 0.5 * (a - c) / np.where(denom == 0.0, 1.0, denom), 0.0)
    return i + np.clip(shift, -0.5, 0.5)


def _refine_scalar(row: np.ndarray, k: int) -> float:
    if k <= 0 or k >= row.size - 1:
        return float(k)
    a, b, c = row[k - 1], row[k], row[k + 1]
    denom = a - 2.0 * b + c
    if abs(denom) < 1e-12:
        return float(k)
    return float(k + np.clip(0.5 * (a - c) / denom, -0.5, 0.5))


def _viterbi(
    obs: np.ndarray,
    voiced_prob: np.ndarray,
    max_step_bins: int,
    switch_prob: float,
    step_sigma_bins: float,
) -> np.ndarray:
    """Decode the 2N-state (voiced/unvoiced x pitch bin) pitch HMM.

    Returns the chosen pitch-bin index per frame, negative where unvoiced.
    """
    n_frames, n_bins = obs.shape
    k = max_step_bins
    offsets = np.arange(-k, k + 1)
    move = -0.5 * (offsets / max(step_sigma_bins, 1e-6)) ** 2
    move -= np.log(np.exp(move).sum())

    log_stay_v = np.log(1.0 - switch_prob)
    log_leave_v = np.log(switch_prob)
    log_stay_u = np.log(1.0 - switch_prob)
    log_leave_u = np.log(switch_prob)

    eps = 1e-12
    obs_v = np.log(np.maximum(obs, eps))
    unvoiced_mass = np.maximum(1.0 - voiced_prob, eps) / n_bins
    obs_u = np.log(np.repeat(unvoiced_mass[:, None], n_bins, axis=1))

    score_v = obs_v[0] + np.log(0.5 / n_bins)
    score_u = obs_u[0] + np.log(0.5 / n_bins)
    bp = np.empty((n_frames, 2 * n_bins), dtype=np.int16)
    bp[0] = -1

    from numpy.lib.stride_tricks import sliding_window_view

    for t in range(1, n_frames):
        pv = np.pad(score_v, k, constant_values=-np.inf)
        pu = np.pad(score_u, k, constant_values=-np.inf)
        wv = sliding_window_view(pv, 2 * k + 1) + move
        wu = sliding_window_view(pu, 2 * k + 1) + move
        av = np.argmax(wv, axis=1)
        au = np.argmax(wu, axis=1)
        rows = np.arange(n_bins)
        bv, bu = wv[rows, av], wu[rows, au]
        src_v = rows + av - k
        src_u = rows + au - k

        cand_v = np.stack([bv + log_stay_v, bu + log_leave_u])
        cand_u = np.stack([bv + log_leave_v, bu + log_stay_u])
        pick_v = np.argmax(cand_v, axis=0)
        pick_u = np.argmax(cand_u, axis=0)

        new_v = cand_v[pick_v, rows] + obs_v[t]
        new_u = cand_u[pick_u, rows] + obs_u[t]
        bp[t, :n_bins] = np.where(pick_v == 0, src_v, src_u + n_bins)
        bp[t, n_bins:] = np.where(pick_u == 0, src_v, src_u + n_bins)

        m = max(new_v.max(), new_u.max())
        score_v, score_u = new_v - m, new_u - m

    state = int(np.argmax(np.concatenate([score_v, score_u])))
    path = np.empty(n_frames, dtype=np.int32)
    for t in range(n_frames - 1, -1, -1):
        path[t] = state if state < n_bins else -(state - n_bins) - 1
        prev = bp[t, state]
        if prev < 0:
            break
        state = int(prev)
    return path


def track_f0(
    x: np.ndarray,
    sample_rate: int,
    fmin: float = 65.0,
    fmax: float = 1100.0,
    frame_len: int | None = None,
    hop: int = 256,
    bin_cents: float = 10.0,
    switch_prob: float = 0.02,
    max_step_cents: float = 250.0,
    chunk_frames: int = 12000,
    block_frames: int = 1024,
) -> PitchTrack:
    """Estimate F0 for a monophonic signal.

    ``fmin``/``fmax`` bracket the singing range; the defaults span a low bass
    fundamental up to a soprano's top notes.  Analysis runs in blocks so that
    a full-length take never materialises a frames-by-lags matrix at once.
    """
    x = np.asarray(x, dtype=np.float64)
    if frame_len is None:
        frame_len = 2048 if sample_rate <= 48000 else 4096
    max_lag = frame_len // 2
    if sample_rate / fmin > max_lag - 2:
        fmin = sample_rate / (max_lag - 2)

    lo_c = 1200.0 * np.log2(fmin / 10.0)
    hi_c = 1200.0 * np.log2(fmax / 10.0)
    bins = np.arange(lo_c, hi_c + bin_cents, bin_cents)
    bin_hz = 10.0 * 2.0 ** (bins / 1200.0)

    pad = frame_len // 2
    xp = np.pad(x, pad, mode="reflect" if x.size > frame_len else "constant")
    n_frames = max(1 + (xp.size - frame_len) // hop, 1)

    obs = np.zeros((n_frames, bins.size), dtype=np.float32)
    vprob = np.zeros(n_frames)
    periodicity = np.zeros(n_frames)
    candidates: list[np.ndarray] = []

    for start in range(0, n_frames, block_frames):
        stop = min(start + block_frames, n_frames)
        idx = np.arange(frame_len)[None, :] + hop * np.arange(start, stop)[:, None]
        block = np.take(xp, np.minimum(idx, xp.size - 1))
        d = cmndf(block, max_lag)
        o, v, p, cand = _observations(d, sample_rate, fmin, fmax, bins)
        obs[start:stop] = o
        vprob[start:stop] = v
        periodicity[start:stop] = p
        candidates.extend(cand)

    # Decode in overlapping chunks so memory stays bounded on long takes.
    overlap = min(200, max(n_frames // 10, 1))
    path = np.empty(n_frames, dtype=np.int32)
    start = 0
    while start < n_frames:
        stop = min(start + chunk_frames, n_frames)
        lo = max(0, start - overlap)
        seg = _viterbi(
            obs[lo:stop].astype(np.float64),
            vprob[lo:stop],
            max_step_bins=max(1, int(round(max_step_cents / bin_cents))),
            switch_prob=switch_prob,
            step_sigma_bins=max(1.0, 35.0 / bin_cents),
        )
        path[start:stop] = seg[start - lo :]
        start = stop

    f0 = np.full(n_frames, np.nan)
    voiced = np.flatnonzero(path >= 0)
    for t in voiced:
        target = bin_hz[path[t]]
        cand = candidates[t] if t < len(candidates) else None
        if cand is not None and cand.size:
            # Snap the decoded bin onto the frame's own refined trough.
            f0[t] = cand[int(np.argmin(np.abs(np.log(cand / target))))]
        else:
            f0[t] = target

    return PitchTrack(
        f0=f0,
        voiced_prob=vprob,
        times=frame_times(n_frames, hop, sample_rate),
        sample_rate=sample_rate,
        hop=hop,
        periodicity=periodicity,
    )
