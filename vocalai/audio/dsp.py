"""Small signal-processing helpers shared by the analysis and render stages."""

from __future__ import annotations

import warnings

import numpy as np
from scipy import signal


def frame_signal(x: np.ndarray, frame_len: int, hop: int, center: bool = True) -> np.ndarray:
    """Cut ``x`` into overlapping frames, one frame per row."""
    x = np.asarray(x, dtype=np.float64)
    if center:
        x = np.pad(x, frame_len // 2, mode="reflect" if x.size > frame_len else "constant")
    if x.size < frame_len:
        x = np.pad(x, (0, frame_len - x.size))
    n = 1 + (x.size - frame_len) // hop
    idx = np.arange(frame_len)[None, :] + hop * np.arange(n)[:, None]
    return x[idx]


def frame_times(n_frames: int, hop: int, sample_rate: int) -> np.ndarray:
    """Centre time, in seconds, of each frame produced by :func:`frame_signal`."""
    return np.arange(n_frames) * hop / float(sample_rate)


def stft(x: np.ndarray, n_fft: int = 2048, hop: int = 512) -> np.ndarray:
    """Magnitude STFT with a periodic Hann window."""
    frames = frame_signal(x, n_fft, hop)
    win = np.hanning(n_fft + 1)[:-1]
    return np.abs(np.fft.rfft(frames * win, axis=1))


def rms_envelope(x: np.ndarray, frame_len: int, hop: int) -> np.ndarray:
    frames = frame_signal(x, frame_len, hop)
    return np.sqrt(np.mean(frames**2, axis=1) + 1e-20)


def db(x: np.ndarray | float, floor: float = -120.0) -> np.ndarray | float:
    return np.maximum(20.0 * np.log10(np.maximum(np.abs(x), 1e-12)), floor)


def hz_to_cents(hz: np.ndarray | float, ref: float = 10.0) -> np.ndarray | float:
    """Absolute cents above ``ref`` Hz.  Unvoiced (<=0) frames stay NaN."""
    hz = np.asarray(hz, dtype=np.float64)
    out = np.full(hz.shape, np.nan)
    ok = hz > 0
    out[ok] = 1200.0 * np.log2(hz[ok] / ref)
    return out if out.shape else float(out)


def cents_to_hz(cents: np.ndarray | float, ref: float = 10.0) -> np.ndarray:
    return ref * np.power(2.0, np.asarray(cents, dtype=np.float64) / 1200.0)


def midi_to_hz(midi: np.ndarray | float, a4: float = 440.0) -> np.ndarray | float:
    return a4 * np.power(2.0, (np.asarray(midi, dtype=np.float64) - 69.0) / 12.0)


def hz_to_midi(hz: np.ndarray | float, a4: float = 440.0) -> np.ndarray | float:
    hz = np.asarray(hz, dtype=np.float64)
    return 69.0 + 12.0 * np.log2(np.maximum(hz, 1e-9) / a4)


NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note_name(midi: float) -> str:
    m = int(round(midi))
    return f"{NOTE_NAMES[m % 12]}{m // 12 - 1}"


def highpass(x: np.ndarray, sample_rate: int, cutoff: float = 60.0, order: int = 4) -> np.ndarray:
    sos = signal.butter(order, cutoff / (sample_rate / 2.0), btype="highpass", output="sos")
    return signal.sosfiltfilt(sos, x)


def lowpass(x: np.ndarray, sample_rate: int, cutoff: float, order: int = 4) -> np.ndarray:
    nyq = sample_rate / 2.0
    wn = min(cutoff / nyq, 0.99)
    sos = signal.butter(order, wn, btype="lowpass", output="sos")
    return signal.sosfiltfilt(sos, x)


def smooth(x: np.ndarray, width: int) -> np.ndarray:
    """Zero-phase moving average that tolerates NaNs by ignoring them.

    The window is forced odd: an even-length kernel under ``mode="same"`` is
    off-centre, and the resulting half-sample delay would show up downstream as
    a small constant timing offset on every warp and correction curve.
    """
    if width <= 1:
        return np.asarray(x, dtype=np.float64)
    width = int(width) | 1
    x = np.asarray(x, dtype=np.float64)
    ok = np.isfinite(x)
    filled = np.where(ok, x, 0.0)
    kern = np.ones(width) / width
    num = np.convolve(filled, kern, mode="same")
    den = np.convolve(ok.astype(np.float64), kern, mode="same")
    out = np.divide(num, den, out=np.full_like(num, np.nan), where=den > 1e-9)
    out[~ok] = np.nan
    return out


def median_filter_nan(x: np.ndarray, width: int) -> np.ndarray:
    """Median filter that leaves NaN (unvoiced) frames untouched."""
    x = np.asarray(x, dtype=np.float64)
    if width <= 1 or x.size == 0:
        return x.copy()
    if width % 2 == 0:
        width += 1
    half = width // 2
    padded = np.pad(x, half, mode="edge")
    idx = np.arange(width)[None, :] + np.arange(x.size)[:, None]
    win = padded[idx]
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", RuntimeWarning)  # all-NaN windows are expected
        out = np.nanmedian(np.where(np.isfinite(win), win, np.nan), axis=1)
    out[~np.isfinite(x)] = np.nan
    return out


def interpolate_gaps(x: np.ndarray, max_gap: int | None = None) -> np.ndarray:
    """Linearly bridge NaN runs; runs longer than ``max_gap`` are left alone."""
    x = np.asarray(x, dtype=np.float64).copy()
    ok = np.isfinite(x)
    if not ok.any() or ok.all():
        return x
    idx = np.arange(x.size)
    filled = np.interp(idx, idx[ok], x[ok])
    if max_gap is None:
        out = filled
        out[: idx[ok][0]] = np.nan
        out[idx[ok][-1] + 1 :] = np.nan
        return out
    out = x.copy()
    for start, stop in nan_runs(x):
        if start == 0 or stop == x.size:
            continue  # never extrapolate past the edges
        if stop - start <= max_gap:
            out[start:stop] = filled[start:stop]
    return out


def nan_runs(x: np.ndarray) -> list[tuple[int, int]]:
    """Half-open [start, stop) index ranges of consecutive NaNs."""
    bad = ~np.isfinite(np.asarray(x, dtype=np.float64))
    if not bad.any():
        return []
    edges = np.diff(np.concatenate(([0], bad.view(np.int8), [0])))
    starts = np.flatnonzero(edges == 1)
    stops = np.flatnonzero(edges == -1)
    return list(zip(starts.tolist(), stops.tolist()))


def true_runs(mask: np.ndarray) -> list[tuple[int, int]]:
    """Half-open index ranges where a boolean mask is True."""
    mask = np.asarray(mask, dtype=bool)
    if not mask.any():
        return []
    edges = np.diff(np.concatenate(([0], mask.view(np.int8), [0])))
    starts = np.flatnonzero(edges == 1)
    stops = np.flatnonzero(edges == -1)
    return list(zip(starts.tolist(), stops.tolist()))


def resample_poly(x: np.ndarray, src_rate: int, dst_rate: int) -> np.ndarray:
    """Band-limited rational resampling (used to match tracks of differing rates)."""
    if src_rate == dst_rate:
        return np.asarray(x, dtype=np.float64)
    g = np.gcd(int(src_rate), int(dst_rate))
    return signal.resample_poly(x, dst_rate // g, src_rate // g, axis=0).astype(np.float64)


def hann(n: int) -> np.ndarray:
    """Symmetric Hann window; the ``n``-point window sums to 1 at 50% overlap."""
    return np.hanning(n)


def normalize_peak(x: np.ndarray, target: float = 0.99) -> np.ndarray:
    peak = float(np.max(np.abs(x))) if x.size else 0.0
    return x * (target / peak) if peak > target else x
