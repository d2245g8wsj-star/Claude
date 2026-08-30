"""Spectral features: onsets, MFCCs, chroma and voice-quality descriptors.

Note segmentation needs onsets (to catch a re-articulated repeated note that
the pitch curve alone cannot show), alignment needs MFCCs, and vocal-track
detection needs timbre statistics.  All of it comes off one shared STFT.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import signal as _sig

from ..audio.dsp import frame_signal, frame_times


@dataclass
class Spectrogram:
    mag: np.ndarray          # (frames, bins)
    freqs: np.ndarray
    times: np.ndarray
    sample_rate: int
    hop: int
    n_fft: int


def spectrogram(x: np.ndarray, sample_rate: int, n_fft: int = 2048, hop: int = 256) -> Spectrogram:
    frames = frame_signal(x, n_fft, hop, center=True)
    win = np.hanning(n_fft + 1)[:-1]
    mag = np.abs(np.fft.rfft(frames * win, axis=1))
    return Spectrogram(
        mag=mag,
        freqs=np.fft.rfftfreq(n_fft, 1.0 / sample_rate),
        times=frame_times(mag.shape[0], hop, sample_rate),
        sample_rate=sample_rate,
        hop=hop,
        n_fft=n_fft,
    )


def onset_envelope(spec: Spectrogram, lag: int = 2, log_compress: float = 1.0) -> np.ndarray:
    """Half-wave-rectified spectral flux on a log-compressed magnitude."""
    m = np.log1p(spec.mag / max(log_compress, 1e-9) * 100.0)
    diff = m[lag:] - m[:-lag]
    flux = np.sum(np.maximum(diff, 0.0), axis=1)
    flux = np.concatenate([np.zeros(lag), flux])
    # Subtract a local mean so the envelope is comparable across dynamics.
    win = 21
    kern = np.ones(win) / win
    local = np.convolve(flux, kern, mode="same")
    out = np.maximum(flux - local, 0.0)
    peak = out.max()
    return out / peak if peak > 0 else out


def pick_onsets(env: np.ndarray, times: np.ndarray, delta: float = 0.06, min_gap: float = 0.05) -> np.ndarray:
    """Peak-pick an onset envelope; returns onset times in seconds."""
    if env.size < 3:
        return np.zeros(0)
    peaks = np.flatnonzero((env[1:-1] > env[:-2]) & (env[1:-1] >= env[2:]) & (env[1:-1] > delta)) + 1
    if peaks.size == 0:
        return np.zeros(0)
    keep = [int(peaks[0])]
    for p in peaks[1:]:
        if times[p] - times[keep[-1]] >= min_gap:
            keep.append(int(p))
        elif env[p] > env[keep[-1]]:
            keep[-1] = int(p)
    return times[np.array(keep)]


def _mel_filterbank(n_fft: int, sample_rate: int, n_mels: int = 40, fmin: float = 40.0,
                    fmax: float | None = None) -> np.ndarray:
    fmax = fmax or sample_rate / 2.0
    def to_mel(f):
        return 2595.0 * np.log10(1.0 + f / 700.0)
    def from_mel(m):
        return 700.0 * (10.0 ** (m / 2595.0) - 1.0)

    pts = from_mel(np.linspace(to_mel(fmin), to_mel(fmax), n_mels + 2))
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sample_rate)
    fb = np.zeros((n_mels, freqs.size))
    for i in range(n_mels):
        lo, mid, hi = pts[i], pts[i + 1], pts[i + 2]
        left = (freqs - lo) / max(mid - lo, 1e-9)
        right = (hi - freqs) / max(hi - mid, 1e-9)
        fb[i] = np.maximum(0.0, np.minimum(left, right))
        norm = fb[i].sum()
        if norm > 0:
            fb[i] /= norm
    return fb


def mfcc(spec: Spectrogram, n_mfcc: int = 20, n_mels: int = 40) -> np.ndarray:
    """MFCCs, one row per frame (coefficient 0 kept: it carries loudness)."""
    fb = _mel_filterbank(spec.n_fft, spec.sample_rate, n_mels)
    mel = np.log(np.maximum(spec.mag**2 @ fb.T, 1e-12))
    n = mel.shape[1]
    k = np.arange(n)
    basis = np.cos(np.pi / n * (k[None, :] + 0.5) * np.arange(n_mfcc)[:, None])
    return mel @ basis.T


def chroma(spec: Spectrogram, a4: float = 440.0) -> np.ndarray:
    """12-bin pitch-class energy profile per frame."""
    freqs = spec.freqs
    ok = freqs > 55.0
    midi = 69.0 + 12.0 * np.log2(np.maximum(freqs[ok], 1e-9) / a4)
    pc = np.mod(np.round(midi).astype(int), 12)
    out = np.zeros((spec.mag.shape[0], 12))
    energy = spec.mag[:, ok] ** 2
    for c in range(12):
        sel = pc == c
        if sel.any():
            out[:, c] = energy[:, sel].sum(axis=1)
    norm = out.sum(axis=1, keepdims=True)
    return np.divide(out, norm, out=np.zeros_like(out), where=norm > 1e-12)


def spectral_stats(spec: Spectrogram) -> dict[str, np.ndarray]:
    """Per-frame centroid, spread, flatness, rolloff and band energies."""
    mag = spec.mag + 1e-12
    power = mag**2
    total = power.sum(axis=1)
    freqs = spec.freqs
    centroid = (power @ freqs) / np.maximum(total, 1e-12)
    spread = np.sqrt(
        np.maximum((power @ (freqs**2)) / np.maximum(total, 1e-12) - centroid**2, 0.0)
    )
    geo = np.exp(np.mean(np.log(mag), axis=1))
    arith = np.mean(mag, axis=1)
    flatness = geo / np.maximum(arith, 1e-12)

    cumulative = np.cumsum(power, axis=1)
    thresh = 0.85 * total[:, None]
    idx = np.argmax(cumulative >= thresh, axis=1)
    rolloff = freqs[np.clip(idx, 0, freqs.size - 1)]

    def band(lo, hi):
        sel = (freqs >= lo) & (freqs < hi)
        return power[:, sel].sum(axis=1) / np.maximum(total, 1e-12)

    return {
        "centroid": centroid,
        "spread": spread,
        "flatness": flatness,
        "rolloff": rolloff,
        "low": band(0.0, 120.0),
        "body": band(120.0, 1000.0),
        "presence": band(1000.0, 5000.0),
        "sibilance": band(5000.0, 11000.0),
        "energy": total,
    }


def harmonic_ratio(x: np.ndarray, sample_rate: int, hop: int = 256, frame_len: int = 2048) -> np.ndarray:
    """Normalised autocorrelation peak per frame - a cheap harmonicity measure."""
    frames = frame_signal(x, frame_len, hop, center=True)
    frames = frames - frames.mean(axis=1, keepdims=True)
    nfft = int(2 ** np.ceil(np.log2(2 * frame_len)))
    spec = np.fft.rfft(frames, nfft)
    acf = np.fft.irfft(spec * np.conj(spec), nfft)[:, :frame_len]
    zero = acf[:, :1]
    lo = int(sample_rate / 1100.0)
    hi = min(int(sample_rate / 60.0), frame_len - 1)
    if hi <= lo:
        return np.zeros(frames.shape[0])
    peak = acf[:, lo:hi].max(axis=1)
    return np.clip(peak / np.maximum(zero[:, 0], 1e-12), 0.0, 1.0)


def loudness_envelope(x: np.ndarray, sample_rate: int, hop: int = 256, frame_len: int = 1024) -> np.ndarray:
    """A-weighted-ish RMS envelope in dB, one value per frame."""
    sos = _sig.butter(2, 100.0 / (sample_rate / 2.0), btype="highpass", output="sos")
    weighted = _sig.sosfilt(sos, x)
    frames = frame_signal(weighted, frame_len, hop, center=True)
    rms = np.sqrt(np.mean(frames**2, axis=1) + 1e-20)
    return 20.0 * np.log10(np.maximum(rms, 1e-10))
