"""Segment a continuous pitch curve into discrete sung notes.

This is the step that separates a note editor from a plain pitch corrector.
A singer's F0 curve does not come pre-cut: it slides into notes, wobbles with
vibrato, breaks for consonants, and repeats the same pitch twice without ever
returning to silence.  The segmenter therefore combines three cues -
sustained pitch change, unvoiced gaps, and spectral onsets - and then finds
each note's stable *core*, which is the only part a tuner may safely move.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from ..audio.dsp import cents_to_hz, interpolate_gaps, median_filter_nan, true_runs
from ..model import Note, NoteSequence
from .features import Spectrogram, loudness_envelope, onset_envelope, pick_onsets, spectrogram
from .pitch import PitchTrack

CENTS_REF = 10.0
VIBRATO_MIN_HZ = 3.5
VIBRATO_MAX_HZ = 9.0


@dataclass
class SegmentConfig:
    min_note: float = 0.055           # notes shorter than this are ornaments, not notes
    min_core: float = 0.030
    split_cents: float = 62.0         # pitch step that separates two notes
    merge_cents: float = 34.0         # below this, two segments are one note
    max_gap: float = 0.075            # unvoiced gap that still keeps a note together
    center_window: float = 0.171      # ~one vibrato cycle: long enough to ignore wobble
    onset_delta: float = 0.07
    core_tolerance: float = 40.0      # cents band that defines "settled on the note"
    transition_window: float = 0.09   # two cues this close describe one boundary
    max_glide: float = 0.20           # a sweep this short is a transition, not a note
    glide_span: float = 66.0          # cents a transition sweeps through
    glide_monotonic: float = 0.78     # how single-mindedly it must travel


def _rolling_median(x: np.ndarray, frames: int) -> np.ndarray:
    return median_filter_nan(x, max(3, frames | 1))


def _binary_split(cents: np.ndarray, lo: int, hi: int, cfg: SegmentConfig, min_frames: int,
                  out: list[int]) -> None:
    """Recursively split [lo, hi) wherever the two halves' medians disagree."""
    n = hi - lo
    if n < 2 * min_frames:
        return
    seg = cents[lo:hi]
    if not np.isfinite(seg).any():
        return
    best_pos, best_gap = -1, 0.0
    # Evaluate candidate cuts on a coarse grid, then refine around the winner.
    for step in (max(min_frames // 2, 2), 1):
        lo_b = max(min_frames, best_pos - step * 2) if best_pos > 0 else min_frames
        hi_b = min(n - min_frames, best_pos + step * 2) if best_pos > 0 else n - min_frames
        for k in range(lo_b, hi_b + 1, step):
            left, right = seg[:k], seg[k:]
            if not (np.isfinite(left).any() and np.isfinite(right).any()):
                continue
            gap = abs(np.nanmedian(right) - np.nanmedian(left))
            if gap > best_gap:
                best_gap, best_pos = gap, k
    if best_pos < 0 or best_gap < cfg.split_cents:
        return
    cut = lo + best_pos
    out.append(cut)
    _binary_split(cents, lo, cut, cfg, min_frames, out)
    _binary_split(cents, cut, hi, cfg, min_frames, out)


def _refine_cut(cents: np.ndarray, cut: int, radius: int) -> int:
    """Move a cut onto the steepest point of the glide between two notes."""
    lo = max(1, cut - radius)
    hi = min(cents.size - 1, cut + radius)
    if hi <= lo:
        return cut
    seg = cents[lo - 1 : hi + 1]
    if not np.isfinite(seg).all():
        return cut
    slope = np.abs(np.diff(seg))
    return lo + int(np.argmax(slope))


def _vibrato(core: np.ndarray, frame_rate: float, min_depth: float = 6.0) -> tuple[float, float]:
    """Rate (Hz) and depth (cents, amplitude) of vibrato on a note core.

    Depth is measured from the band-limited component only, so a note that
    merely sags in pitch is not mistaken for one that is being modulated.
    """
    n = core.size
    if n < int(0.18 * frame_rate) or not np.isfinite(core).all():
        return 0.0, 0.0
    idx = np.arange(n)
    detrended = core - np.polyval(np.polyfit(idx, core, 1), idx)

    # Padded transform for rate resolution.
    nfft = max(1024, 2 ** int(np.ceil(np.log2(n * 4))))
    spec = np.abs(np.fft.rfft(detrended * np.hanning(n), n=nfft))
    freqs = np.fft.rfftfreq(nfft, 1.0 / frame_rate)
    band = (freqs >= VIBRATO_MIN_HZ) & (freqs <= VIBRATO_MAX_HZ)
    low = (freqs >= 0.2) & (freqs <= 20.0)
    if not band.any() or spec[band].max() <= 0:
        return 0.0, 0.0
    # Both sums span the same padded grid, so the ratio is padding-invariant.
    share = float(np.sum(spec[band] ** 2) / max(np.sum(spec[low] ** 2), 1e-12))
    if share < 0.30:
        return 0.0, 0.0
    rate = float(freqs[np.flatnonzero(band)[int(np.argmax(spec[band]))]])

    # Depth from the band-limited residual alone.
    coeff = np.fft.rfft(detrended)
    f_un = np.fft.rfftfreq(n, 1.0 / frame_rate)
    coeff[(f_un < VIBRATO_MIN_HZ) | (f_un > VIBRATO_MAX_HZ)] = 0.0
    depth = float(np.sqrt(2.0) * np.std(np.fft.irfft(coeff, n)))
    if depth < min_depth:
        return 0.0, 0.0
    return rate, depth


def segment_notes(
    track: PitchTrack,
    audio: np.ndarray,
    spec: Spectrogram | None = None,
    cfg: SegmentConfig | None = None,
    source: str | None = None,
) -> NoteSequence:
    """Cut a :class:`PitchTrack` into notes."""
    cfg = cfg or SegmentConfig()
    sr, hop = track.sample_rate, track.hop
    frame_rate = sr / hop
    n_frames = track.f0.size

    spec = spec or spectrogram(audio, sr, hop=hop)
    onsets = pick_onsets(onset_envelope(spec), spec.times, delta=cfg.onset_delta)
    loud = loudness_envelope(audio, sr, hop=hop)

    cents = np.where(np.isfinite(track.f0), 1200.0 * np.log2(np.maximum(track.f0, 1e-9) / CENTS_REF), np.nan)
    # Bridge consonant-length dropouts so one word is not chopped into pieces.
    bridged = interpolate_gaps(cents, max_gap=int(cfg.max_gap * frame_rate))
    smooth_c = _rolling_median(bridged, int(0.029 * frame_rate))
    center_c = _rolling_median(bridged, int(cfg.center_window * frame_rate))

    min_frames = max(2, int(cfg.min_note * frame_rate))
    notes: list[Note] = []

    for lo, hi in true_runs(np.isfinite(bridged)):
        if hi - lo < min_frames:
            continue
        cuts: list[int] = []
        _binary_split(center_c, lo, hi, cfg, min_frames, cuts)

        # A repeated note re-articulates without a pitch change: trust the onset.
        for t in onsets:
            f = int(round(t * frame_rate))
            if lo + min_frames <= f <= hi - min_frames:
                if all(abs(f - c) > min_frames for c in cuts):
                    cuts.append(f)

        radius = max(1, int(0.030 * frame_rate))
        # Refine first, then de-duplicate: refinement moves cuts, and two cues
        # pointing at the same transition must not leave a sliver between them.
        cuts = sorted(_refine_cut(smooth_c, c, radius) for c in cuts)
        cuts = _dedupe(cuts, max(min_frames, int(cfg.transition_window * frame_rate)))
        bounds = [lo] + [c for c in cuts if lo < c < hi] + [hi]
        bounds = _merge_segments(
            bounds, center_c, cfg, onsets, frame_rate,
            max(min_frames, int(cfg.transition_window * frame_rate)),
        )

        bounds = _dissolve_glides(bounds, center_c, smooth_c, cfg, frame_rate)

        for a, b in zip(bounds[:-1], bounds[1:]):
            note = _build_note(
                len(notes), a, b, bridged, smooth_c, center_c, track, loud, frame_rate, cfg
            )
            if note is not None:
                notes.append(note)

    for i, n in enumerate(notes):
        n.index = i
    return NoteSequence(notes=notes, sample_rate=sr, hop=hop, source=source)


def _dedupe(cuts: list[int], min_frames: int) -> list[int]:
    """Collapse cuts that landed within one minimum-note distance of each other."""
    out: list[int] = []
    for c in sorted(cuts):
        if out and c - out[-1] < min_frames:
            continue
        out.append(c)
    return out


def _merge_segments(bounds: list[int], center_c: np.ndarray, cfg: SegmentConfig,
                    onsets: np.ndarray, frame_rate: float, min_frames: int) -> list[int]:
    """Drop cuts that separate two segments of effectively the same pitch.

    A cut that leaves a fragment shorter than a real note always goes: such a
    sliver is the tail of a glide, not a note the singer performed.
    """
    out = list(bounds)
    changed = True
    while changed and len(out) > 2:
        changed = False
        # Slivers first, cheapest cut removed.
        lengths = [(out[i + 1] - out[i], i) for i in range(len(out) - 1)]
        short = [(ln, i) for ln, i in lengths if ln < min_frames]
        if short:
            _, i = min(short)
            # Remove whichever boundary of the sliver is not a track edge.
            drop = i if i > 0 else i + 1
            if 0 < drop < len(out) - 1:
                del out[drop]
                changed = True
                continue

        for i in range(1, len(out) - 1):
            a, cut, b = out[i - 1], out[i], out[i + 1]
            left = center_c[a:cut]
            right = center_c[cut:b]
            if not (np.isfinite(left).any() and np.isfinite(right).any()):
                continue
            gap = abs(np.nanmedian(right) - np.nanmedian(left))
            near_onset = np.any(np.abs(onsets - cut / frame_rate) < 0.035)
            too_short = (cut - a) < min_frames or (b - cut) < min_frames
            if gap < cfg.merge_cents and (not near_onset or too_short):
                del out[i]
                changed = True
                break
    return out


def _dissolve_glides(bounds: list[int], center_c: np.ndarray, smooth_c: np.ndarray,
                     cfg: SegmentConfig, frame_rate: float) -> list[int]:
    """Remove segments that are a slide between two notes rather than a note.

    A sung note always has somewhere it settles.  A segment whose centre sweeps
    steadily through a large interval and never holds still is the glide of a
    leap, and cutting it out as its own note is how a segmenter invents pitches
    the singer never sang.  Such a segment is dissolved and replaced by a single
    boundary at the steepest point of the slide.
    """
    max_frames = int(cfg.max_glide * frame_rate)
    changed = True
    while changed and len(bounds) > 2:
        changed = False
        for i in range(len(bounds) - 1):
            a, b = bounds[i], bounds[i + 1]
            seg = center_c[a:b]
            seg = seg[np.isfinite(seg)]
            if seg.size < 3 or (b - a) > max_frames:
                continue
            span = float(seg.max() - seg.min())
            if span < cfg.glide_span:
                continue
            steps = np.diff(seg)
            travel = float(np.abs(steps.sum()))
            path = float(np.abs(steps).sum())
            if path <= 0 or travel / path < cfg.glide_monotonic:
                continue  # it wobbles, so it is a note being sung, not a slide

            if i == 0:
                del bounds[1]
            elif i == len(bounds) - 2:
                del bounds[-2]
            else:
                cut = _refine_cut(smooth_c, (a + b) // 2, (b - a) // 2)
                bounds[i] = cut
                del bounds[i + 1]
            changed = True
            break
    return bounds


def _build_note(index: int, a: int, b: int, cents: np.ndarray, smooth_c: np.ndarray,
                center_c: np.ndarray, track: PitchTrack, loud: np.ndarray,
                frame_rate: float, cfg: SegmentConfig) -> Note | None:
    seg = smooth_c[a:b]
    if seg.size < 2 or not np.isfinite(seg).any():
        return None

    # The centre is read from the middle of the note, away from glides.
    inner_lo = a + int(0.2 * (b - a))
    inner_hi = b - int(0.15 * (b - a))
    inner = smooth_c[inner_lo:inner_hi] if inner_hi > inner_lo else seg
    center = float(np.nanmedian(inner if np.isfinite(inner).any() else seg))

    core_a, core_b = _find_core(smooth_c, a, b, center, cfg, frame_rate)
    core = smooth_c[core_a:core_b]
    if core.size >= 3 and np.isfinite(core).any():
        center = float(np.nanmedian(core))

    if (b - a) / frame_rate < cfg.min_note:
        return None

    rate, depth = _vibrato(np.nan_to_num(core, nan=center), frame_rate)
    if core.size >= 3:
        idx = np.arange(core.size)
        fin = np.isfinite(core)
        slope = np.polyfit(idx[fin], core[fin], 1)[0] if fin.sum() >= 2 else 0.0
        drift = float(slope * core.size)
    else:
        drift = 0.0

    head = smooth_c[a : min(a + max(1, int(0.02 * frame_rate)), b)]
    onset_off = float(np.nanmedian(head) - center) if np.isfinite(head).any() else 0.0

    lo_f = min(a, loud.size - 1)
    hi_f = min(max(b, lo_f + 1), loud.size)
    amp = loud[lo_f:hi_f]
    voiced = track.voiced[a:b] if b <= track.voiced.size else track.voiced[a:]
    conf = track.voiced_prob[a:b]

    return Note(
        index=index,
        start=a / frame_rate,
        end=b / frame_rate,
        core_start=core_a / frame_rate,
        core_end=core_b / frame_rate,
        center_hz=float(cents_to_hz(center, ref=CENTS_REF)),
        amplitude_db=float(np.mean(amp)) if amp.size else -60.0,
        peak_db=float(np.max(amp)) if amp.size else -60.0,
        confidence=float(np.mean(conf)) if conf.size else 0.0,
        voiced_ratio=float(np.mean(voiced)) if voiced.size else 0.0,
        vibrato_hz=rate,
        vibrato_cents=depth,
        drift_cents=drift,
        onset_cents=onset_off,
    )


def _find_core(smooth_c: np.ndarray, a: int, b: int, center: float, cfg: SegmentConfig,
               frame_rate: float) -> tuple[int, int]:
    """Trim the approach and release glides, leaving the settled part of the note."""
    n = b - a
    max_trim = int(0.45 * n)
    tol = cfg.core_tolerance
    hold = max(1, int(0.025 * frame_rate))

    start = a
    for i in range(a, a + max_trim):
        window = smooth_c[i : i + hold]
        if np.isfinite(window).all() and np.all(np.abs(window - center) <= tol):
            start = i
            break
    else:
        start = a + min(max_trim, int(0.15 * n))

    stop = b
    for i in range(b, b - max_trim, -1):
        window = smooth_c[max(i - hold, a) : i]
        if window.size and np.isfinite(window).all() and np.all(np.abs(window - center) <= tol):
            stop = i
            break
    else:
        stop = b - min(max_trim, int(0.1 * n))

    if (stop - start) / frame_rate < cfg.min_core:
        mid = (a + b) // 2
        half = max(1, int(0.5 * cfg.min_core * frame_rate))
        start, stop = max(a, mid - half), min(b, mid + half)
    return start, stop
