"""Decide which tracks in a session are vocals, and how they relate.

Pointing a tuner at every file in a session folder is how a snare ends up
transposed.  This module scores each track on cues that separate a sung voice
from a guitar, a synth pad or a drum loop, and then works out which vocal is
the lead and which are doubles or harmonies stacked against it.

The classifier is a transparent weighted rubric rather than a black box: every
track carries the evidence for its verdict, so a wrong call can be seen and
overridden instead of merely disbelieved.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from ..audio.dsp import true_runs
from ..model import NoteSequence
from .features import Spectrogram, harmonic_ratio, loudness_envelope, spectral_stats
from .pitch import PitchTrack


@dataclass
class TrackProfile:
    """What a track looks like, and whether we think anyone sang it."""

    name: str
    is_vocal: bool = False
    score: float = 0.0
    evidence: dict[str, float] = field(default_factory=dict)
    role: str = "unknown"             # lead | double | harmony | instrument
    lead_of: str | None = None
    interval_semitones: float | None = None
    median_f0: float = 0.0
    loudness_db: float = -60.0
    mean_active_db: float = -60.0
    active_ratio: float = 0.0

    def explain(self) -> str:
        bits = ", ".join(f"{k}={v:.2f}" for k, v in self.evidence.items())
        return f"{self.name}: {self.role} (score {self.score:.2f}) [{bits}]"


def _band_score(value: float, lo: float, hi: float, soft: float) -> float:
    """1.0 inside [lo, hi], tapering to 0 over ``soft`` outside it."""
    if lo <= value <= hi:
        return 1.0
    d = (lo - value) if value < lo else (value - hi)
    return float(max(0.0, 1.0 - d / max(soft, 1e-9)))


def profile_track(
    name: str,
    audio: np.ndarray,
    sample_rate: int,
    pitch: PitchTrack,
    spec: Spectrogram,
    notes: NoteSequence | None = None,
) -> TrackProfile:
    """Score one track on how much it behaves like a sung vocal."""
    stats = spectral_stats(spec)
    loud = loudness_envelope(audio, sample_rate, hop=pitch.hop)
    n = min(loud.size, pitch.f0.size, stats["energy"].size)
    loud, f0 = loud[:n], pitch.f0[:n]
    voiced = np.isfinite(f0)

    # "Active" = the parts of the track that are actually playing.
    peak = float(np.max(loud)) if n else -120.0
    active = loud > (peak - 35.0)
    active_ratio = float(active.mean()) if n else 0.0
    if active.sum() < 8:
        return TrackProfile(name=name, is_vocal=False, score=0.0, role="instrument",
                            active_ratio=active_ratio, loudness_db=peak,
                            mean_active_db=peak)

    voiced_ratio = float(voiced[active].mean())
    median_f0 = float(np.nanmedian(f0[voiced])) if voiced.any() else 0.0

    # Consonants: unvoiced frames carrying high-band energy.  Few instruments
    # alternate between pitched and noisy this way; speech and singing always do.
    sib = stats["sibilance"][:n]
    sibilant = (~voiced) & active & (sib > 0.16)
    sibilance_ratio = float(sibilant.sum() / max(active.sum(), 1))

    # Continuous pitch movement: a voice slides between notes, a keyboard steps.
    glide_ratio = 0.0
    if voiced.sum() > 8:
        cents = 1200.0 * np.log2(np.maximum(f0, 1e-9) / 10.0)
        rate = np.abs(np.diff(cents)) * (sample_rate / pitch.hop)
        ok = np.isfinite(rate)
        if ok.any():
            glide_ratio = float(np.mean((rate[ok] > 40.0) & (rate[ok] < 2500.0)))

    low = float(np.mean(stats["low"][:n][active]))
    flatness = float(np.mean(stats["flatness"][:n][active]))
    hnr = float(np.mean(harmonic_ratio(audio, sample_rate, hop=pitch.hop)[:n][active]))

    vibrato_ratio = 0.0
    if notes is not None and len(notes):
        vibrato_ratio = float(np.mean([nn.vibrato_cents > 8.0 for nn in notes.notes]))

    # Continuous pitch movement is the single most vocal-specific cue: a voice
    # slides into notes and wobbles on them, whereas a plucked or struck
    # instrument jumps between discrete pitches and holds them dead still.
    expression = max(min(glide_ratio / 0.55, 1.0), min(vibrato_ratio / 0.35, 1.0))

    evidence = {
        "voiced_ratio": _band_score(voiced_ratio, 0.30, 0.92, 0.30),
        "f0_range": _band_score(median_f0, 75.0, 800.0, 120.0) if median_f0 > 0 else 0.0,
        "sibilance": min(sibilance_ratio / 0.10, 1.0),
        "expression": expression,
        "low_end_clear": _band_score(low, 0.0, 0.10, 0.14),
        "tonal": min(hnr / 0.45, 1.0),
        "not_noisy": _band_score(flatness, 0.0, 0.32, 0.25),
    }
    weights = {
        "voiced_ratio": 1.2,
        "f0_range": 1.2,
        "sibilance": 1.8,
        "expression": 3.0,
        "low_end_clear": 1.0,
        "tonal": 0.8,
        "not_noisy": 0.8,
    }
    score = sum(evidence[k] * w for k, w in weights.items()) / sum(weights.values())

    # Hard vetoes: things a vocal simply is not.
    if median_f0 <= 0 or voiced_ratio < 0.12:
        score = min(score, 0.2)
    if low > 0.35:                     # bass or kick energy dominates
        score = min(score, 0.25)
    if expression < 0.25 and evidence["sibilance"] < 0.5:
        # Steady discrete pitches with no consonants: an instrument, not a take.
        score = min(score, 0.35)

    is_vocal = score >= 0.55
    return TrackProfile(
        name=name,
        is_vocal=is_vocal,
        score=float(score),
        evidence={k: float(v) for k, v in evidence.items()},
        role="unknown" if is_vocal else "instrument",
        median_f0=median_f0,
        loudness_db=peak,
        mean_active_db=float(np.mean(loud[active])),
        active_ratio=active_ratio,
    )


def _onset_similarity(a: np.ndarray, b: np.ndarray) -> float:
    """Correlation of two loudness envelopes, allowing a small time offset."""
    n = min(a.size, b.size)
    if n < 16:
        return 0.0
    x = a[:n] - a[:n].mean()
    y = b[:n] - b[:n].mean()
    denom = np.linalg.norm(x) * np.linalg.norm(y)
    if denom < 1e-9:
        return 0.0
    lag = min(int(0.25 * n), 200)
    corr = np.correlate(x, y, mode="full") / denom
    mid = corr.size // 2
    return float(np.max(corr[mid - lag : mid + lag + 1]))


def _median_interval(a: NoteSequence, b: NoteSequence, tol: float = 0.20) -> float | None:
    """Median pitch interval between time-overlapping notes of two takes."""
    if not len(a) or not len(b):
        return None
    intervals = []
    b_times = np.array([n.core_start for n in b.notes])
    for note in a.notes:
        if b_times.size == 0:
            break
        k = int(np.argmin(np.abs(b_times - note.core_start)))
        if abs(b_times[k] - note.core_start) > tol:
            continue
        intervals.append(b.notes[k].midi - note.midi)
    if len(intervals) < 3:
        return None
    return float(np.median(intervals))


def group_vocals(
    profiles: dict[str, TrackProfile],
    envelopes: dict[str, np.ndarray],
    sequences: dict[str, NoteSequence],
    double_cents: float = 90.0,
    name_roles: dict[str, str] | None = None,
) -> None:
    """Assign lead / double / harmony roles across the vocal tracks in place.

    Getting this backwards is expensive - it would warp the lead onto a double
    - so an explicit name ("lead vox", "BV 3rd") outranks the audio evidence.
    Engineers label the lead for the same reason we read the label.
    """
    vocals = [p for p in profiles.values() if p.is_vocal]
    if not vocals:
        return
    if len(vocals) == 1:
        vocals[0].role = "lead"
        return

    name_roles = name_roles or {}
    named_leads = [p for p in vocals if name_roles.get(p.name) == "lead"]
    named_support = {p.name for p in vocals if name_roles.get(p.name) == "support"}

    # Failing an explicit label: the lead is the vocal that carries the most of
    # the song, loudest and most continuously - stacks sing less and sit lower.
    def lead_score(p: TrackProfile) -> float:
        base = p.mean_active_db / 10.0 + 4.0 * p.active_ratio + p.score
        return base - (1.5 if p.name in named_support else 0.0)

    lead = named_leads[0] if named_leads else max(vocals, key=lead_score)
    lead.role = "lead"

    for p in vocals:
        if p is lead:
            continue
        sim = _onset_similarity(envelopes.get(p.name, np.zeros(0)),
                                envelopes.get(lead.name, np.zeros(0)))
        interval = _median_interval(sequences.get(lead.name, NoteSequence()),
                                    sequences.get(p.name, NoteSequence()))
        p.lead_of = lead.name
        p.interval_semitones = interval
        if interval is None:
            p.role = "double" if sim > 0.5 else "lead"
            continue
        if abs(interval) * 100.0 <= double_cents:
            p.role = "double"
        elif abs(interval) <= 12.5:
            p.role = "harmony"
        else:
            # More than an octave apart with different material: its own part.
            p.role = "double" if sim > 0.6 else "lead"
            p.lead_of = lead.name if p.role == "double" else None
