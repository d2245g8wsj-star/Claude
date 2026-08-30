"""The editable note model - this project's equivalent of a Melodyne blob.

Analysis produces :class:`Note` objects; the tuning engine decides a *target*
for each; the renderer turns the difference into a pitch-shift curve.  Every
stage in between is inspectable and hand-editable, which is the whole point of
a note-based editor: the algorithm's decision is a starting proposal, not a
verdict.
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path

import numpy as np

from .audio.dsp import hz_to_midi, midi_to_hz, note_name


@dataclass
class Note:
    """One detected sung note."""

    index: int
    start: float                 # first frame of the note, transitions included
    end: float
    core_start: float            # the stable centre, excluding attack/release glide
    core_end: float
    center_hz: float             # robust pitch of the core
    amplitude_db: float = -60.0
    peak_db: float = -60.0
    confidence: float = 1.0
    voiced_ratio: float = 1.0
    vibrato_hz: float = 0.0
    vibrato_cents: float = 0.0   # half the peak-to-peak modulation depth
    drift_cents: float = 0.0     # slow pitch movement across the core
    onset_cents: float = 0.0     # how far the attack scoops from the centre

    # --- decisions written by the tuning engine ---
    target_midi: float | None = None
    target_hz: float | None = None
    correction_cents: float = 0.0     # what will actually be applied at the core
    strength: float = 1.0             # 0..1 amount of the error removed
    scale_degree: str | None = None
    reason: str = ""

    # --- manual edits, applied on top of the automatic decision ---
    pitch_offset_cents: float = 0.0
    formant_shift_semitones: float = 0.0
    gain_db: float = 0.0
    vibrato_scale: float = 1.0
    drift_scale: float = 1.0
    time_offset: float = 0.0
    locked: bool = False              # True = renderer leaves this note alone

    @property
    def duration(self) -> float:
        return max(self.end - self.start, 0.0)

    @property
    def core_duration(self) -> float:
        return max(self.core_end - self.core_start, 0.0)

    @property
    def midi(self) -> float:
        return float(hz_to_midi(self.center_hz))

    @property
    def nearest_midi(self) -> int:
        return int(round(self.midi))

    @property
    def cents_off(self) -> float:
        """Deviation of the sung centre from the nearest equal-tempered note."""
        return (self.midi - self.nearest_midi) * 100.0

    @property
    def name(self) -> str:
        return note_name(self.midi)

    @property
    def target_name(self) -> str | None:
        return note_name(self.target_midi) if self.target_midi is not None else None

    def target_frequency(self, tuning_hz: float = 440.0) -> float | None:
        """The target in Hz, derived from ``target_midi`` when it is set.

        ``target_midi`` is the musical decision and ``target_hz`` is a cached
        rendering of it, so a hand-edited note file that moves the MIDI value
        must win - otherwise an edit appears to be accepted and then quietly
        does nothing.
        """
        if self.target_midi is not None:
            return float(tuning_hz * 2.0 ** ((self.target_midi - 69.0) / 12.0))
        return self.target_hz

    @property
    def error_to_target_cents(self) -> float:
        if self.target_midi is None:
            return 0.0
        return (self.midi - self.target_midi) * 100.0

    def to_dict(self) -> dict:
        d = asdict(self)
        d.update(
            name=self.name,
            midi=round(self.midi, 4),
            cents_off=round(self.cents_off, 2),
            duration=round(self.duration, 4),
            target_name=self.target_name,
        )
        return d


@dataclass
class NoteSequence:
    """All notes of one track, plus the musical context they were read in."""

    notes: list[Note] = field(default_factory=list)
    sample_rate: int = 44100
    hop: int = 256
    tuning_hz: float = 440.0
    key: str | None = None
    source: str | None = None

    def __len__(self) -> int:
        return len(self.notes)

    def __iter__(self):
        return iter(self.notes)

    def __getitem__(self, i: int) -> Note:
        return self.notes[i]

    def at(self, seconds: float) -> Note | None:
        for n in self.notes:
            if n.start <= seconds < n.end:
                return n
        return None

    def total_error_cents(self) -> float:
        """Mean absolute distance from the equal-tempered grid, duration weighted."""
        if not self.notes:
            return 0.0
        w = np.array([n.core_duration for n in self.notes])
        e = np.array([abs(n.cents_off) for n in self.notes])
        return float(np.average(e, weights=w) if w.sum() > 0 else e.mean())

    def to_json(self, path: str | Path | None = None, indent: int = 2) -> str:
        payload = {
            "sample_rate": self.sample_rate,
            "hop": self.hop,
            "tuning_hz": self.tuning_hz,
            "key": self.key,
            "source": self.source,
            "notes": [n.to_dict() for n in self.notes],
        }
        text = json.dumps(payload, indent=indent)
        if path is not None:
            Path(path).write_text(text)
        return text

    @classmethod
    def from_json(cls, path_or_text: str | Path) -> "NoteSequence":
        text = (
            Path(path_or_text).read_text()
            if isinstance(path_or_text, Path) or str(path_or_text).endswith(".json")
            else str(path_or_text)
        )
        data = json.loads(text)
        fields = {f for f in Note.__dataclass_fields__}
        notes = [Note(**{k: v for k, v in nd.items() if k in fields}) for nd in data["notes"]]
        return cls(
            notes=notes,
            sample_rate=data.get("sample_rate", 44100),
            hop=data.get("hop", 256),
            tuning_hz=data.get("tuning_hz", 440.0),
            key=data.get("key"),
            source=data.get("source"),
        )

    def summary(self) -> str:
        lines = [
            f"{len(self.notes)} notes  key={self.key or '?'}  "
            f"tuning={self.tuning_hz:.2f} Hz  mean error={self.total_error_cents():.1f} cents"
        ]
        for n in self.notes:
            tgt = f"-> {n.target_name}" if n.target_name else ""
            lines.append(
                f"  [{n.index:3d}] {n.start:7.3f}s {n.duration:5.2f}s  "
                f"{n.name:>4} {n.cents_off:+6.1f}c {tgt:>7}  "
                f"vib {n.vibrato_cents:4.0f}c/{n.vibrato_hz:3.1f}Hz  {n.reason}"
            )
        return "\n".join(lines)
