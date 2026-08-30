"""A synthetic multitrack session, for demos, tests and benchmarks.

Produces a lead vocal that is out of tune, a double and a harmony that are out
of tune *and* out of time with the lead, plus guitar, bass and drums that must
be left alone.  Ground truth for every vocal is written alongside as JSON, so
the result of a run can be scored rather than merely listened to.
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from ..audio.io import write_wav
from .synth import (
    NoteSpec,
    VoiceSpec,
    render_bass,
    render_drums,
    render_plucked,
    render_voice,
)

SR = 44100
# A short phrase in A minor, with the harmony a diatonic third above it -
# which is what a singer stacking a part actually does, so the harmony stays
# inside the key rather than sitting a fixed number of semitones away.
MELODY = [69, 71, 72, 74, 72, 71, 69, 67, 69]
HARMONY = [72, 74, 76, 77, 76, 74, 72, 71, 72]
DUR = 0.52
GAP = 0.06
START = 0.25


def _voice(midis, detune, offsets, vibrato, vowels, seed, level=1.0):
    notes = []
    t = START
    rng = np.random.default_rng(seed)
    for i, m in enumerate(midis):
        notes.append(
            NoteSpec(
                midi=m,
                start=t + offsets[i],
                duration=DUR,
                vowel=vowels[i % len(vowels)],
                level=level,
                detune_cents=detune[i],
                onset_cents=float(rng.uniform(-80.0, -25.0)),
                vibrato_cents=vibrato,
                vibrato_hz=float(rng.uniform(4.9, 6.1)),
                drift_cents=float(rng.normal(0.0, 14.0)),
            )
        )
        t += DUR + GAP
    return VoiceSpec(notes=notes, seed=seed)


def build(out_dir: Path) -> dict:
    rng = np.random.default_rng(7)
    n = len(MELODY)

    # Real takes sit within about a half-semitone of the note.  Beyond that the
    # intended pitch is genuinely ambiguous - a note 70 cents flat *is* the note
    # below, slightly sharp - so the demo stays inside what is recoverable.
    def detune(scale, seed_rng):
        return list(np.clip(seed_rng.normal(0.0, scale, n), -45.0, 45.0))

    lead_detune = detune(22.0, rng)
    dbl_detune = detune(26.0, rng)
    har_detune = detune(24.0, rng)
    dbl_offsets = list(rng.uniform(-0.055, 0.055, n))
    har_offsets = list(rng.uniform(-0.070, 0.070, n))

    lead, _, _ = render_voice(_voice(MELODY, lead_detune, [0.0] * n, 38.0, "aeiou", 1))
    dbl, _, _ = render_voice(_voice(MELODY, dbl_detune, dbl_offsets, 30.0, "aeiou", 5, 0.8))
    har, _, _ = render_voice(
        _voice(HARMONY, har_detune, har_offsets, 26.0, "aeiou", 9, 0.7)
    )

    chords = [69, 72, 76, 74, 72, 71, 69, 67, 69]
    gtr = render_plucked(chords, dur=DUR, gap=GAP, start=START, seed=3)
    bass = render_bass([m - 24 for m in chords], dur=DUR + GAP, gap=0.0, start=START)
    drums = render_drums(bars=3, bpm=104.0)

    out_dir.mkdir(parents=True, exist_ok=True)
    tracks = {
        "01_Lead_Vox": lead,
        "02_Vox_Double": dbl,
        "03_Vox_Harmony_3rd": har,
        "04_Gtr": gtr,
        "05_Bass_DI": bass,
        "06_Drums": drums,
    }
    for name, audio in tracks.items():
        write_wav(out_dir / f"{name}.wav", audio, SR, bits=24)

    truth = {
        "melody_midi": MELODY,
        "harmony_midi": HARMONY,
        "note_starts": [round(START + i * (DUR + GAP), 4) for i in range(n)],
        "lead_detune_cents": [round(x, 2) for x in lead_detune],
        "double_detune_cents": [round(x, 2) for x in dbl_detune],
        "harmony_detune_cents": [round(x, 2) for x in har_detune],
        "double_time_offsets_s": [round(x, 4) for x in dbl_offsets],
        "harmony_time_offsets_s": [round(x, 4) for x in har_offsets],
        "key": "A minor",
        "vocal_tracks": ["01_Lead_Vox", "02_Vox_Double", "03_Vox_Harmony_3rd"],
    }
    (out_dir / "ground_truth.json").write_text(json.dumps(truth, indent=2))
    return truth
