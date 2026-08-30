"""Key, scale and concert-pitch estimation.

Before an engineer corrects a single note they work out two things: what the
session is tuned to (not every record is at A=440), and what key it is in, so
that a slightly flat E in a C-minor song is pulled to E-flat rather than to E.
This module answers both from the detected notes themselves.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from ..audio.dsp import NOTE_NAMES
from ..model import NoteSequence

# Krumhansl-Kessler key profiles.
MAJOR_PROFILE = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
MINOR_PROFILE = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])

# Below this circular concentration the notes are too scattered to imply a reference.
MIN_TUNING_CONCENTRATION = 0.45

SCALES: dict[str, tuple[int, ...]] = {
    "major": (0, 2, 4, 5, 7, 9, 11),
    "minor": (0, 2, 3, 5, 7, 8, 10),
    "harmonic_minor": (0, 2, 3, 5, 7, 8, 11),
    "melodic_minor": (0, 2, 3, 5, 7, 9, 11),
    "dorian": (0, 2, 3, 5, 7, 9, 10),
    "mixolydian": (0, 2, 4, 5, 7, 9, 10),
    "pentatonic_major": (0, 2, 4, 7, 9),
    "pentatonic_minor": (0, 3, 5, 7, 10),
    "blues": (0, 3, 5, 6, 7, 10),
    "chromatic": tuple(range(12)),
}


@dataclass
class KeyEstimate:
    """A detected key, with the per-pitch-class prior the tuner will use."""

    tonic: int = 0
    mode: str = "major"
    confidence: float = 0.0
    tuning_hz: float = 440.0
    weights: np.ndarray = field(default_factory=lambda: np.ones(12) / 12.0)
    scale_name: str = "major"

    @property
    def name(self) -> str:
        return f"{NOTE_NAMES[self.tonic % 12]} {self.mode}"

    @property
    def pitch_classes(self) -> tuple[int, ...]:
        base = SCALES.get(self.scale_name, SCALES["major"])
        return tuple(sorted((self.tonic + d) % 12 for d in base))

    def contains(self, midi: float) -> bool:
        return int(round(midi)) % 12 in self.pitch_classes

    def degree_name(self, midi: float) -> str:
        pc = int(round(midi)) % 12
        interval = (pc - self.tonic) % 12
        base = SCALES.get(self.scale_name, SCALES["major"])
        roman = ["I", "bII", "II", "bIII", "III", "IV", "bV", "V", "bVI", "VI", "bVII", "VII"]
        tag = roman[interval]
        return tag if interval in base else f"({tag})"


def estimate_tuning(sequence: NoteSequence | list[NoteSequence], a4: float = 440.0) -> float:
    """Estimate the session's concert pitch from how notes sit on the grid.

    Every note's offset from the equal-tempered grid is an angle on a circle of
    100 cents; the circular mean of those angles is the global tuning offset,
    which is robust to individual notes being sharp or flat.
    """
    seqs = sequence if isinstance(sequence, list) else [sequence]
    offsets, weights = [], []
    for seq in seqs:
        for n in seq.notes:
            if n.core_duration < 0.08 or n.confidence < 0.4:
                continue
            midi = 69.0 + 12.0 * np.log2(max(n.center_hz, 1e-9) / a4)
            offsets.append((midi - round(midi)) * 100.0)
            weights.append(n.core_duration)
    if len(offsets) < 4:
        return a4

    angles = np.array(offsets) * 2.0 * np.pi / 100.0
    w = np.array(weights)
    resultant = np.sum(w * np.exp(1j * angles)) / w.sum()
    # The resultant length says how tightly the notes cluster around one offset.
    # A loosely tuned take has no meaningful reference at all, and inventing one
    # would shift every note name in the song, so leave it at concert pitch.
    concentration = float(np.abs(resultant))
    if concentration < MIN_TUNING_CONCENTRATION:
        return a4
    # Shrink toward concert pitch in proportion to how tight the cluster is, so
    # a barely-significant reading nudges rather than lurches.
    shrink = float(np.clip((concentration - MIN_TUNING_CONCENTRATION) / 0.30, 0.0, 1.0))
    cents = float(np.angle(resultant) * 100.0 / (2.0 * np.pi)) * shrink
    # A drift beyond a sixth of a tone is far likelier to be a bad read than a
    # session actually tuned that far off.
    cents = float(np.clip(cents, -35.0, 35.0))
    return float(a4 * 2.0 ** (cents / 1200.0))


def pitch_class_histogram(
    sequences: NoteSequence | list[NoteSequence], tuning_hz: float = 440.0
) -> np.ndarray:
    """Duration- and loudness-weighted pitch-class profile."""
    seqs = sequences if isinstance(sequences, list) else [sequences]
    hist = np.zeros(12)
    for seq in seqs:
        for n in seq.notes:
            if n.confidence < 0.35:
                continue
            midi = 69.0 + 12.0 * np.log2(max(n.center_hz, 1e-9) / tuning_hz)
            pc = int(round(midi)) % 12
            loud = 10.0 ** (n.amplitude_db / 20.0)
            hist[pc] += max(n.core_duration, 0.02) * (0.3 + loud)
    return hist


def estimate_key(
    sequences: NoteSequence | list[NoteSequence],
    tuning_hz: float | None = None,
    scale_name: str | None = None,
) -> KeyEstimate:
    """Correlate the pitch-class profile against all 24 major/minor keys."""
    seqs = sequences if isinstance(sequences, list) else [sequences]
    if tuning_hz is None:
        tuning_hz = estimate_tuning(seqs)
    hist = pitch_class_histogram(seqs, tuning_hz)
    if hist.sum() <= 0:
        return KeyEstimate(tuning_hz=tuning_hz)

    x = hist - hist.mean()
    best = (-2.0, 0, "major")
    scores = []
    for tonic in range(12):
        for mode, profile in (("major", MAJOR_PROFILE), ("minor", MINOR_PROFILE)):
            p = np.roll(profile, tonic)
            p = p - p.mean()
            denom = np.linalg.norm(x) * np.linalg.norm(p)
            r = float(x @ p / denom) if denom > 0 else 0.0
            scores.append(r)
            if r > best[0]:
                best = (r, tonic, mode)

    r, tonic, mode = best
    scores = np.sort(np.array(scores))[::-1]
    margin = float(scores[0] - scores[1]) if scores.size > 1 else 0.0
    confidence = float(np.clip(0.5 * max(r, 0.0) + 2.5 * margin, 0.0, 1.0))

    resolved_scale = scale_name or ("major" if mode == "major" else "minor")
    weights = _scale_weights(tonic, resolved_scale, hist)
    return KeyEstimate(
        tonic=tonic,
        mode=mode,
        confidence=confidence,
        tuning_hz=tuning_hz,
        weights=weights,
        scale_name=resolved_scale,
    )


def _scale_weights(tonic: int, scale_name: str, hist: np.ndarray) -> np.ndarray:
    """Prior probability that a given pitch class is the intended note.

    Scale tones start high, but a chromatic tone the singer actually used a lot
    is promoted - blues thirds and passing tones are intentional, and forcing
    them into the scale is the classic sound of a bad auto-tune job.
    """
    scale = SCALES.get(scale_name, SCALES["major"])
    in_scale = np.array([(pc - tonic) % 12 in scale for pc in range(12)])
    weights = np.where(in_scale, 1.0, 0.16)
    if hist.sum() > 0:
        usage = hist / hist.sum()
        weights = weights + 1.4 * usage
    return weights / weights.sum()


def key_from_name(text: str, tuning_hz: float = 440.0) -> KeyEstimate:
    """Parse a key such as ``"F# minor"``, ``"Bb dorian"`` or ``"chromatic"``."""
    t = text.strip().lower().replace("-", " ")
    if t in ("chromatic", "none", "off"):
        return KeyEstimate(
            tonic=0, mode="chromatic", confidence=1.0, tuning_hz=tuning_hz,
            weights=np.ones(12) / 12.0, scale_name="chromatic",
        )
    parts = t.split()
    root = parts[0] if parts else "c"
    flats = {"db": 1, "eb": 3, "gb": 6, "ab": 8, "bb": 10}
    naturals = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}
    if root in flats:
        tonic = flats[root]
    elif root.replace("#", "") in naturals:
        tonic = (naturals[root.replace("#", "")] + root.count("#")) % 12
    else:
        raise ValueError(f"unrecognised key root: {text!r}")

    mode = " ".join(parts[1:]) or "major"
    mode = mode.replace(" ", "_")
    aliases = {"maj": "major", "min": "minor", "m": "minor", "": "major",
               "harmonic": "harmonic_minor", "melodic": "melodic_minor"}
    scale_name = aliases.get(mode, mode)
    if scale_name not in SCALES:
        raise ValueError(f"unknown scale: {mode!r} (have {', '.join(sorted(SCALES))})")
    return KeyEstimate(
        tonic=tonic,
        mode="minor" if "minor" in scale_name else scale_name,
        confidence=1.0,
        tuning_hz=tuning_hz,
        weights=_scale_weights(tonic, scale_name, np.zeros(12)),
        scale_name=scale_name,
    )
