"""The tuning engine: decide what was *meant*, then correct it like a person.

Two ideas separate this from a snap-to-nearest-semitone plugin.

First, targets are chosen by a Viterbi decode over the whole melody rather than
note by note.  The cost balances three things a musician weighs at once: how
close the target is to what was sung, how well it fits the key, and - most
importantly - whether it preserves the interval the singer actually performed.
A phrase sung consistently 40 cents flat should be pulled into tune without its
melody being rewritten.

Second, correction is applied to a *decomposition* of the pitch curve, not to
the curve itself.  The slow component (where the note sits, and how it sags)
is what gets moved; vibrato, jitter and the glides into and out of a phrase are
handed back untouched.  Removing those is what makes tuning sound synthetic.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np
from scipy import signal as _sig

from ..analysis.key import KeyEstimate
from ..analysis.notes import CENTS_REF
from ..analysis.pitch import PitchTrack
from ..audio.dsp import interpolate_gaps, smooth
from ..model import Note, NoteSequence


@dataclass
class TuneConfig:
    """How aggressively, and how musically, to correct."""

    strength: float = 0.88            # fraction of the error removed on the core
    vibrato_keep: float = 1.0         # 1.0 = vibrato passes through untouched
    drift_keep: float = 0.45          # how much slow in-note pitch sag survives
    preserve_transitions: bool = True # keep scoops into and falls out of phrases
    transition_max: float = 0.16      # longest glide that may be preserved (s)
    key_weight: float = 1.1           # pull toward scale tones
    melodic_weight: float = 1.0       # pull toward melodies that behave like melodies
    pitch_sigma: float = 50.0         # assumed spread of a singer's pitch error (cents)
    drift_notes: int = 6              # window, in notes, for tracking a singer going flat
    max_drift_cents: float = 30.0
    max_shift_cents: float = 320.0    # refuse to move a note further than this
    min_note_strength: float = 0.35   # short ornaments only get partial treatment
    bend_threshold: float = 62.0      # beyond this the note is treated as a bend
    smooth_ms: float = 22.0           # final smoothing of the correction curve
    candidates: int = 2               # semitones searched either side of the note

    @classmethod
    def style(cls, name: str) -> "TuneConfig":
        """Presets spanning transparent repair to the hard auto-tune effect."""
        presets = {
            "transparent": cls(strength=0.68, drift_keep=0.75, vibrato_keep=1.0),
            "natural": cls(),
            "tight": cls(strength=0.96, drift_keep=0.25, vibrato_keep=0.9),
            "hard": cls(
                strength=1.0,
                vibrato_keep=0.0,
                drift_keep=0.0,
                preserve_transitions=False,
                smooth_ms=6.0,
                min_note_strength=1.0,
            ),
        }
        if name not in presets:
            raise ValueError(f"unknown style {name!r}; have {', '.join(presets)}")
        return presets[name]


@dataclass
class TuneResult:
    """Everything the renderer and the report need."""

    correction_cents: np.ndarray      # per frame
    ratio: np.ndarray                 # per frame, 2**(cents/1200)
    sequence: NoteSequence
    key: KeyEstimate
    stats: dict = field(default_factory=dict)


# Relative frequency of melodic intervals in tonal song writing: steps dominate,
# leaps thin out quickly, and the tritone is close to nonexistent.
_INTERVAL_P = np.array(
    [0.130, 0.100, 0.210, 0.100, 0.070, 0.070, 0.010, 0.045, 0.014, 0.020, 0.010, 0.006, 0.020]
)


def _melodic_logp(steps: np.ndarray) -> np.ndarray:
    """Log prior over melodic intervals, in semitones."""
    k = np.abs(np.rint(steps)).astype(int)
    p = np.where(k <= 12, _INTERVAL_P[np.minimum(k, 12)], 0.002)
    # Beyond an octave, keep decaying rather than dropping to a hard floor.
    far = k > 12
    if far.any():
        p = np.where(far, 0.02 * 0.5 ** (k - 12), p)
    return np.log(np.maximum(p, 1e-6))


def _target_candidates(note: Note, tuning_hz: float, span: int) -> np.ndarray:
    midi = 69.0 + 12.0 * np.log2(max(note.center_hz, 1e-9) / tuning_hz)
    base = int(round(midi))
    return np.arange(base - span, base + span + 1, dtype=float)


def _local_drift(sung: np.ndarray, window: int, limit: float) -> np.ndarray:
    """Track a singer drifting flat or sharp across a phrase.

    Correlated error is real - voices tire, and a whole section can sit under
    the note - so it belongs in the model rather than being mistaken for a run
    of individually wrong notes.  It is deliberately slow and tightly clipped.
    """
    n = sung.size
    offsets = (sung - np.rint(sung)) * 100.0
    if n < max(window, 4):
        return np.zeros(n)
    drift = np.zeros(n)
    angles = offsets * 2.0 * np.pi / 100.0
    for i in range(n):
        lo, hi = max(0, i - window), min(n, i + window + 1)
        resultant = np.mean(np.exp(1j * angles[lo:hi]))
        if np.abs(resultant) < 0.6:
            continue  # the neighbourhood disagrees; assume no drift
        drift[i] = np.angle(resultant) * 100.0 / (2.0 * np.pi)
    return np.clip(drift, -limit, limit)


def assign_targets(seq: NoteSequence, key: KeyEstimate, cfg: TuneConfig) -> None:
    """Choose a target note for every note by decoding the melody as a whole.

    The model is generative: an intended note sequence, drawn from a key prior
    and a melodic-interval prior, is performed with a slow drift plus per-note
    error.  Decoding it recovers what the singer meant.  Note that there is no
    term rewarding the *sung* interval - that evidence is already in the pitch
    of each note, and counting it twice is what makes a decoder invent
    chromatic notes to explain away two ordinary mistakes.
    """
    notes = [n for n in seq.notes if not n.locked]
    if not notes:
        return

    tuning = key.tuning_hz
    cands = [_target_candidates(n, tuning, cfg.candidates) for n in notes]
    sung = np.array([69.0 + 12.0 * np.log2(max(n.center_hz, 1e-9) / tuning) for n in notes])
    drift = _local_drift(sung, cfg.drift_notes, cfg.max_drift_cents)

    def emission(i: int) -> np.ndarray:
        n = notes[i]
        c = cands[i]
        dist = (sung[i] - c) * 100.0 - drift[i]
        score = -((dist / cfg.pitch_sigma) ** 2)
        pc = np.mod(np.round(c).astype(int), 12)
        score = score + cfg.key_weight * np.log(np.maximum(key.weights[pc], 1e-6) * 12.0)
        # Long, confident notes carry more weight in the musical decision.
        weight = np.clip(n.core_duration / 0.25, 0.35, 2.0) * np.clip(n.confidence * 1.5, 0.3, 1.0)
        score = score * weight
        score[np.abs(dist) > cfg.max_shift_cents] = -1e6
        return score

    n_notes = len(notes)
    scores = emission(0)
    back: list[np.ndarray] = [np.full(cands[0].size, -1, dtype=int)]

    for i in range(1, n_notes):
        prev, cur = cands[i - 1], cands[i]
        step = cur[None, :] - prev[:, None]
        trans = cfg.melodic_weight * _melodic_logp(step)
        # A rest loosens the melodic tie between two notes.
        gap = max(notes[i].start - notes[i - 1].end, 0.0)
        trans = trans * float(np.exp(-gap / 1.2))
        total = scores[:, None] + trans
        best = np.argmax(total, axis=0)
        back.append(best)
        scores = total[best, np.arange(cur.size)] + emission(i)

    path = np.empty(n_notes, dtype=int)
    path[-1] = int(np.argmax(scores))
    for i in range(n_notes - 1, 0, -1):
        path[i - 1] = int(back[i][path[i]])

    for i, note in enumerate(notes):
        target = float(cands[i][path[i]])
        note.target_midi = target
        note.target_hz = float(tuning * 2.0 ** ((target - 69.0) / 12.0))
        note.scale_degree = key.degree_name(target)
        note.strength, note.reason = _note_strength(note, cfg)
        note.correction_cents = -note.error_to_target_cents * note.strength


def _note_strength(note: Note, cfg: TuneConfig) -> tuple[float, str]:
    """Decide how hard to correct this particular note, and say why."""
    strength = cfg.strength
    reasons = []

    err = abs(note.error_to_target_cents)
    if err > cfg.bend_threshold and cfg.strength < 1.0:
        # Sitting this far off after a musical decode usually means the note is
        # a deliberate bend or blue note; nudge it rather than snap it.
        strength *= 0.55
        reasons.append("bend")
    if note.core_duration < 0.09:
        strength *= max(cfg.min_note_strength, 0.5)
        reasons.append("short")
    if note.confidence < 0.5:
        strength *= 0.7
        reasons.append("low-confidence")
    if note.vibrato_cents > 90.0:
        strength *= 0.9
        reasons.append("wide-vibrato")
    if abs(note.drift_cents) > 150.0 and cfg.strength < 1.0:
        strength *= 0.75
        reasons.append("slide")

    strength = float(np.clip(strength, 0.0, 1.0))
    return strength, "+".join(reasons)


def _decompose(cents: np.ndarray, frame_rate: float) -> tuple[np.ndarray, np.ndarray]:
    """Split a pitch curve into a slow component and a vibrato component."""
    filled = interpolate_gaps(cents)
    filled = np.where(np.isfinite(filled), filled, np.nanmedian(filled[np.isfinite(filled)])
                      if np.isfinite(filled).any() else 0.0)
    nyq = frame_rate / 2.0
    slow_cut = min(3.0 / nyq, 0.95)
    sos_low = _sig.butter(2, slow_cut, btype="lowpass", output="sos")
    slow = _sig.sosfiltfilt(sos_low, filled) if filled.size > 24 else filled.copy()

    hi = min(9.0 / nyq, 0.95)
    lo = min(3.5 / nyq, hi * 0.9)
    if filled.size > 24 and hi > lo:
        sos_band = _sig.butter(2, [lo, hi], btype="bandpass", output="sos")
        vib = _sig.sosfiltfilt(sos_band, filled)
    else:
        vib = np.zeros_like(filled)
    return slow, vib


def build_correction(
    seq: NoteSequence, track: PitchTrack, key: KeyEstimate, cfg: TuneConfig
) -> np.ndarray:
    """Turn per-note decisions into a smooth per-frame correction in cents."""
    frame_rate = track.sample_rate / track.hop
    n_frames = track.f0.size
    cents = np.where(
        np.isfinite(track.f0), 1200.0 * np.log2(np.maximum(track.f0, 1e-9) / CENTS_REF), np.nan
    )
    slow, vib = _decompose(cents, frame_rate)
    correction = np.zeros(n_frames)

    notes = seq.notes
    for i, note in enumerate(notes):
        target_hz = note.target_frequency(key.tuning_hz)
        if note.locked or target_hz is None:
            continue
        a = int(np.clip(round(note.start * frame_rate), 0, n_frames - 1))
        b = int(np.clip(round(note.end * frame_rate), a + 1, n_frames))
        core_a = int(np.clip(round(note.core_start * frame_rate), a, b - 1))
        core_b = int(np.clip(round(note.core_end * frame_rate), core_a + 1, b))

        target_c = 1200.0 * np.log2(target_hz / CENTS_REF)
        center_c = float(np.median(slow[core_a:core_b]))

        offset = (target_c - center_c) + note.pitch_offset_cents
        drift_keep = np.clip(cfg.drift_keep * note.drift_scale, 0.0, 1.0)
        vib_keep = np.clip(cfg.vibrato_keep * note.vibrato_scale, 0.0, 2.0)

        idx = np.arange(a, b)
        # Move where the note sits; hand back its wobble and its sag.
        seg = (
            offset
            - (1.0 - drift_keep) * (slow[idx] - center_c)
            - (1.0 - vib_keep) * vib[idx]
        )
        env = _envelope(note, notes, i, a, b, core_a, core_b, frame_rate, cfg)
        correction[a:b] = seg * env * note.strength

    if cfg.smooth_ms > 0:
        width = max(1, int(cfg.smooth_ms * frame_rate / 1000.0))
        correction = np.nan_to_num(smooth(correction, width), nan=0.0)
    return np.clip(correction, -cfg.max_shift_cents, cfg.max_shift_cents)


def _envelope(
    note: Note,
    notes: list[Note],
    i: int,
    a: int,
    b: int,
    core_a: int,
    core_b: int,
    frame_rate: float,
    cfg: TuneConfig,
) -> np.ndarray:
    """Ramp correction in and out so performed glides survive.

    A glide only counts as expressive at the edge of a phrase.  Between two
    connected notes the correction stays fully engaged, which lets the glide
    itself travel between two *in-tune* endpoints instead of being flattened.
    """
    n = b - a
    env = np.ones(n)
    if not cfg.preserve_transitions:
        return env

    max_ramp = int(cfg.transition_max * frame_rate)
    prev_gap = note.start - notes[i - 1].end if i > 0 else 1e9
    next_gap = notes[i + 1].start - note.end if i + 1 < len(notes) else 1e9

    if prev_gap > 0.08:  # phrase start: preserve the scoop into the note
        ramp = int(np.clip(core_a - a, int(0.012 * frame_rate), max_ramp))
        ramp = min(ramp, max(n // 2, 1))
        if ramp > 1:
            env[:ramp] = np.linspace(0.0, 1.0, ramp) ** 0.7

    if next_gap > 0.08:  # phrase end: let the release fall away naturally
        ramp = int(np.clip(b - core_b, int(0.010 * frame_rate), max_ramp))
        ramp = min(ramp, max(n // 3, 1))
        if ramp > 1:
            env[-ramp:] = np.minimum(env[-ramp:], np.linspace(1.0, 0.45, ramp))
    return env


def tune(
    seq: NoteSequence,
    track: PitchTrack,
    key: KeyEstimate,
    cfg: TuneConfig | None = None,
) -> TuneResult:
    """Decide targets and build the correction curve for one vocal track."""
    cfg = cfg or TuneConfig()
    assign_targets(seq, key, cfg)
    correction = build_correction(seq, track, key, cfg)
    seq.tuning_hz = key.tuning_hz
    seq.key = key.name

    corrected = [n for n in seq.notes if n.target_midi is not None]
    before = float(np.mean([abs(n.cents_off) for n in corrected])) if corrected else 0.0
    after = (
        float(np.mean([abs(n.error_to_target_cents) * (1.0 - n.strength) for n in corrected]))
        if corrected else 0.0
    )
    moved = [n for n in corrected if abs(n.error_to_target_cents) > 5.0]
    stats = {
        "notes": len(seq.notes),
        "notes_moved": len(moved),
        "mean_error_before_cents": round(before, 2),
        "mean_error_after_cents": round(after, 2),
        "max_shift_cents": round(
            max((abs(n.correction_cents) for n in corrected), default=0.0), 2
        ),
        "key": key.name,
        "tuning_hz": round(key.tuning_hz, 2),
    }
    return TuneResult(
        correction_cents=correction,
        ratio=2.0 ** (correction / 1200.0),
        sequence=seq,
        key=key,
        stats=stats,
    )
