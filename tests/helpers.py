"""Shared fixtures for the test suite."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from vocalai.testing.synth import melody, render_voice  # noqa: E402

SR = 44100


def sung(midis, dur=0.45, detune=None, vibrato=30.0, seed=1):
    """Render a short sung phrase and return (audio, midis)."""
    spec = melody(midis, dur=dur, detune=detune, vibrato=vibrato, seed=seed)
    audio, _, _ = render_voice(spec)
    return audio, list(midis)


def steady(midi=57.0, duration=1.0, vowel="a", seed=1, consonants=False):
    """A single sustained note with no vibrato - the cleanest possible case."""
    from vocalai.testing.synth import NoteSpec, VoiceSpec

    spec = VoiceSpec(
        notes=[NoteSpec(midi=midi, start=0.05, duration=duration, vowel=vowel, vibrato_cents=0.0)],
        seed=seed,
        consonants=consonants,
    )
    audio, truth, _ = render_voice(spec)
    return audio, truth


def cents_between(a: float, b: float) -> float:
    return 1200.0 * np.log2(a / b)
