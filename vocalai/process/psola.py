"""Time-domain PSOLA: pitch-synchronous overlap-add pitch and time modification.

PSOLA is the right tool for vocal tuning because it changes the *spacing* of
glottal pulses without touching their content.  The spectral envelope - the
formants that make a voice sound like a person rather than a chipmunk - rides
along unchanged.  Unvoiced material has no pulses to re-space, so it is copied
verbatim; that is what keeps consonants crisp instead of smeared.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import signal as _sig

from ..audio.dsp import true_runs


@dataclass
class EpochTrack:
    """Glottal pulse positions and the local period at each."""

    marks: np.ndarray       # sample indices, ascending
    periods: np.ndarray     # local period in samples at each mark
    voiced: np.ndarray      # per-sample voicing mask


def detect_epochs(
    x: np.ndarray,
    sample_rate: int,
    f0: np.ndarray,
    hop: int,
    bandwidth_hz: float = 70.0,
) -> EpochTrack:
    """Place exactly one pitch mark per glottal cycle, locked to F0's phase.

    Peak-picking a band-limited copy of the signal is the textbook approach and
    it fails on exactly the material that matters here: a fixed passband cannot
    isolate the fundamental of a voice that ranges over an octave, so on higher
    notes the second harmonic leaks in and the search alternates between two
    peaks per cycle.  The resulting marks are spaced 2T apart half the time,
    and re-spacing *those* renders an octave down.

    Instead the signal is heterodyned by the tracked F0 - shifting the
    fundamental to DC - and low-pass filtered.  The residual phase of that
    complex envelope, added back to the reference phase, is the fundamental's
    own instantaneous phase.  Marks are placed where it crosses each multiple
    of 2*pi, which yields one mark per cycle by construction, at any pitch.
    """
    x = np.asarray(x, dtype=np.float64)
    n = x.size
    per_sample_f0 = _frames_to_samples(f0, hop, n)
    voiced = np.isfinite(per_sample_f0)
    if not voiced.any() or n < 8:
        return EpochTrack(np.zeros(0, dtype=np.int64), np.zeros(0), voiced)

    # A continuous frequency track: unvoiced stretches hold the nearest value
    # so the reference phase stays smooth across them.
    filled = per_sample_f0.copy()
    idx = np.arange(n)
    ok = np.flatnonzero(voiced)
    filled = np.interp(idx, ok, per_sample_f0[ok])

    phase_ref = 2.0 * np.pi * np.cumsum(filled) / sample_rate
    z = x * np.exp(-1j * phase_ref)
    nyq = sample_rate / 2.0
    sos = _sig.butter(3, min(bandwidth_hz / nyq, 0.95), btype="lowpass", output="sos")
    z = _sig.sosfiltfilt(sos, z.real) + 1j * _sig.sosfiltfilt(sos, z.imag)

    phase = phase_ref + np.unwrap(np.angle(z))
    # Guard against the unwrapped phase stalling or reversing on noisy frames.
    phase = np.maximum.accumulate(phase)

    offset = _best_phase_offset(x, phase, voiced)
    lo_k = int(np.ceil((phase[0] - offset) / (2.0 * np.pi)))
    hi_k = int(np.floor((phase[-1] - offset) / (2.0 * np.pi)))
    if hi_k <= lo_k:
        return EpochTrack(np.zeros(0, dtype=np.int64), np.zeros(0), voiced)

    targets = 2.0 * np.pi * np.arange(lo_k, hi_k + 1) + offset
    positions = np.interp(targets, phase, idx)
    marks = np.round(positions).astype(np.int64)
    marks = np.clip(marks, 0, n - 1)

    keep = voiced[marks]
    marks = marks[keep]
    if marks.size < 2:
        return EpochTrack(np.zeros(0, dtype=np.int64), np.zeros(0), voiced)

    periods = sample_rate / np.maximum(filled[marks], 1e-6)
    return EpochTrack(marks, periods, voiced)


def _best_phase_offset(x: np.ndarray, phase: np.ndarray, voiced: np.ndarray) -> float:
    """Pick the phase within the cycle carrying the most energy.

    Any consistent phase works for overlap-add, but centring the windows on the
    loudest instant of the cycle - roughly the glottal closure - keeps each
    window's content where the waveform is strongest.
    """
    sel = voiced & np.isfinite(phase)
    if sel.sum() < 64:
        return 0.0
    wrapped = np.mod(phase[sel], 2.0 * np.pi)
    energy = x[sel] ** 2
    bins = 24
    which = np.minimum((wrapped / (2.0 * np.pi) * bins).astype(int), bins - 1)
    totals = np.bincount(which, weights=energy, minlength=bins)
    return float((np.argmax(totals) + 0.5) / bins * 2.0 * np.pi)


def _frames_to_samples(values: np.ndarray, hop: int, n_samples: int) -> np.ndarray:
    """Expand a frame-rate curve to sample rate, keeping NaN as NaN."""
    idx = np.clip(np.arange(n_samples) // hop, 0, max(values.size - 1, 0))
    return values[idx] if values.size else np.full(n_samples, np.nan)


def psola_modify(
    x: np.ndarray,
    sample_rate: int,
    epochs: EpochTrack,
    pitch_ratio: np.ndarray,
    hop: int,
    time_map: np.ndarray | None = None,
    out_length: int | None = None,
) -> np.ndarray:
    """Re-synthesise ``x`` with a time-varying pitch ratio.

    ``pitch_ratio`` is a frame-rate curve (1.0 = unchanged, 2.0 = an octave up).
    ``time_map`` optionally maps *output* sample index to *input* sample index;
    when omitted, timing is left alone.
    """
    x = np.asarray(x, dtype=np.float64)
    n = x.size
    ratio_s = np.clip(_frames_to_samples(np.nan_to_num(pitch_ratio, nan=1.0), hop, n), 0.25, 4.0)

    if time_map is None:
        out_len = out_length or n
        time_map = np.arange(out_len, dtype=np.float64)
    else:
        time_map = np.asarray(time_map, dtype=np.float64)
        out_len = out_length or time_map.size

    out = np.zeros(out_len)
    norm = np.zeros(out_len)

    if epochs.marks.size >= 2:
        _synthesize_voiced(x, out, norm, epochs, ratio_s, time_map)
    _copy_unvoiced(x, out, norm, epochs.voiced, time_map, sample_rate)

    np.divide(out, np.maximum(norm, 1e-6), out=out, where=norm > 1e-6)
    return out


def _synthesize_voiced(
    x: np.ndarray,
    out: np.ndarray,
    norm: np.ndarray,
    epochs: EpochTrack,
    ratio_s: np.ndarray,
    time_map: np.ndarray,
) -> None:
    """Overlap-add windowed glottal cycles at their new spacing.

    The loop runs once per output pitch period - tens of thousands of times for
    a full take - so everything inside it is scalar indexing.  ``time_map`` is
    already sampled per output sample, which makes the input position a lookup
    rather than an interpolation.
    """
    n = x.size
    out_len = out.size
    marks = epochs.marks
    periods = epochs.periods
    voiced = epochs.voiced
    n_marks = marks.size
    map_len = time_map.size

    out_pos = 0.0
    while out_pos < out_len:
        idx_out = int(out_pos)
        src = time_map[idx_out if idx_out < map_len else map_len - 1]
        i = int(src)
        if i < 0:
            i = 0
        elif i >= n:
            i = n - 1
        if not voiced[i]:
            out_pos += 1.0
            continue

        k = int(np.searchsorted(marks, i))
        if k >= n_marks:
            k = n_marks - 1
        elif k > 0 and abs(marks[k] - i) > abs(marks[k - 1] - i):
            k -= 1
        mark = int(marks[k])
        period = float(periods[k])
        if period < 2.0:
            out_pos += 1.0
            continue

        half = int(round(period))
        lo, hi = mark - half, mark + half
        seg_lo = lo if lo > 0 else 0
        seg_hi = hi if hi < n else n
        if seg_hi - seg_lo < 4:
            out_pos += 1.0
            continue

        win = _hann_cached(2 * half + 1)[: hi - lo]
        w = win[seg_lo - lo : seg_hi - lo]
        seg = x[seg_lo:seg_hi] * w

        dst = int(round(out_pos))
        d_lo = dst - (mark - seg_lo)
        d_hi = dst + (seg_hi - mark)
        c_lo = d_lo if d_lo > 0 else 0
        c_hi = d_hi if d_hi < out_len else out_len
        if c_hi > c_lo:
            a, b = c_lo - d_lo, c_hi - d_lo
            out[c_lo:c_hi] += seg[a:b]
            norm[c_lo:c_hi] += w[a:b]

        # Step forward by one *output* period: the input period divided by the
        # pitch ratio, further scaled by the local time-stretch factor.
        nxt = idx_out + 1
        stretch = time_map[nxt] - time_map[idx_out] if nxt < map_len else 1.0
        if stretch < 1e-3:
            stretch = 1e-3
        ratio = ratio_s[i]
        step = period / (ratio if ratio > 1e-3 else 1e-3) / stretch
        out_pos += step if step > 2.0 else 2.0


_HANN_CACHE: dict[int, np.ndarray] = {}


def _hann_cached(n: int) -> np.ndarray:
    """Hann windows are requested once per cycle at a handful of lengths."""
    w = _HANN_CACHE.get(n)
    if w is None:
        w = np.hanning(n)
        if len(_HANN_CACHE) > 4096:
            _HANN_CACHE.clear()
        _HANN_CACHE[n] = w
    return w


def _copy_unvoiced(
    x: np.ndarray,
    out: np.ndarray,
    norm: np.ndarray,
    voiced: np.ndarray,
    time_map: np.ndarray,
    sample_rate: int,
) -> None:
    """Pass unvoiced audio through untouched, crossfading at the boundaries."""
    n = x.size
    out_len = out.size
    fade = max(int(0.004 * sample_rate), 8)

    src_idx = np.clip(np.round(time_map).astype(np.int64), 0, n - 1)
    unvoiced_out = ~voiced[src_idx]
    for lo, hi in true_runs(unvoiced_out):
        lo_f = max(lo - fade, 0)
        hi_f = min(hi + fade, out_len)
        seg = x[src_idx[lo_f:hi_f]]
        w = np.ones(hi_f - lo_f)
        head = lo - lo_f
        tail = hi_f - hi
        if head > 0:
            w[:head] = np.linspace(0.0, 1.0, head)
        if tail > 0:
            w[-tail:] = np.linspace(1.0, 0.0, tail)
        out[lo_f:hi_f] += seg * w
        norm[lo_f:hi_f] += w


def wsola_time_warp(
    x: np.ndarray,
    sample_rate: int,
    time_map: np.ndarray,
    frame_len: int | None = None,
    tolerance: int | None = None,
    transients: np.ndarray | None = None,
    transient_tolerance: int | None = None,
) -> np.ndarray:
    """Waveform-similarity overlap-add time warping at constant pitch.

    ``time_map[k]`` is the input sample that should appear at output sample
    ``k``.  Each output frame is taken from near its mapped input position,
    nudged by up to ``tolerance`` samples to the offset that best continues the
    previous frame - which is what keeps the waveform phase-coherent.

    That nudge is exactly what must not happen at a consonant: freedom to slide
    by 10 ms in search of a better waveform match is freedom to put the
    transient in the wrong place, which defeats the point of aligning.  Frames
    landing on a transient therefore get a much tighter search, trading a
    little phase continuity for timing that lands where it was asked to.
    """
    x = np.asarray(x, dtype=np.float64)
    n = x.size
    time_map = np.asarray(time_map, dtype=np.float64)
    out_len = time_map.size
    if n == 0 or out_len == 0:
        return np.zeros(out_len)

    frame_len = frame_len or int(0.030 * sample_rate)
    frame_len = max(frame_len // 2 * 2, 64)
    half = frame_len // 2
    tolerance = tolerance if tolerance is not None else int(0.008 * sample_rate)
    transient_tolerance = (
        transient_tolerance if transient_tolerance is not None else max(int(0.001 * sample_rate), 8)
    )
    marks = np.sort(np.asarray(transients, dtype=np.float64)) if transients is not None else None

    win = np.hanning(frame_len + 1)[:-1]
    out = np.zeros(out_len + frame_len)
    norm = np.zeros(out_len + frame_len)

    prev_tail = np.zeros(half)
    out_positions = np.arange(0, out_len, half)
    targets = np.interp(out_positions, np.arange(out_len), time_map)

    for out_start, target_f in zip(out_positions, targets):
        target = int(round(target_f))
        tol = tolerance
        if marks is not None and marks.size:
            k = int(np.clip(np.searchsorted(marks, target_f), 0, marks.size - 1))
            near = min(
                abs(marks[k] - target_f),
                abs(marks[max(k - 1, 0)] - target_f),
            )
            if near < half:
                tol = transient_tolerance

        if out_start == 0:
            best = target
        else:
            lo = max(target - tol, 0)
            hi = min(target + tol, max(n - frame_len, 0))
            best = (
                _best_offset(x, prev_tail, lo, hi, half, target=target)
                if hi > lo
                else min(target, max(n - frame_len, 0))
            )
        best = int(np.clip(best, 0, max(n - frame_len, 0)))
        seg = np.zeros(frame_len)
        take = min(frame_len, n - best)
        if take <= 0:
            break
        seg[:take] = x[best : best + take]
        out[out_start : out_start + frame_len] += seg * win
        norm[out_start : out_start + frame_len] += win
        prev_tail = seg[half:].copy()

    np.divide(out, np.maximum(norm, 1e-6), out=out, where=norm > 1e-6)
    return out[:out_len]


def _best_offset(
    x: np.ndarray,
    template: np.ndarray,
    lo: int,
    hi: int,
    half: int,
    target: int | None = None,
    bias: float = 0.08,
) -> int:
    """Offset in [lo, hi] whose leading half best continues the previous tail.

    Textbook WSOLA takes the similarity maximum outright.  That is unsafe here:
    over silence every candidate scores identically, the tie breaks toward an
    arbitrary edge of the search range, and because each frame then continues
    from the drifted one, the output stays offset from the timing that was
    requested.  A quadratic pull toward the mapped position costs nothing when
    the similarity peak is real and settles the matter when it is not.
    """
    if hi <= lo or template.size == 0:
        return target if target is not None else lo
    span = min(hi - lo + half, x.size - lo)
    if span <= half:
        return target if target is not None else lo
    from numpy.lib.stride_tricks import sliding_window_view

    view = sliding_window_view(x[lo : lo + span], half)
    if view.shape[0] == 0:
        return target if target is not None else lo

    t_norm = float(np.linalg.norm(template))
    if t_norm < 1e-9:
        return target if target is not None else lo
    energy = np.sqrt(np.sum(view**2, axis=1)) + 1e-9
    score = (view @ template) / (energy * t_norm)      # cosine similarity

    if target is not None:
        offsets = np.arange(view.shape[0]) + lo - target
        reach = max(hi - target, target - lo, 1)
        score = score - bias * (offsets / reach) ** 2
    return lo + int(np.argmax(score))
