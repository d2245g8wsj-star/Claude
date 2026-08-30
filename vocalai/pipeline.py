"""Track-level API: analyse a vocal, tune it, and render the result."""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .analysis.features import Spectrogram, spectrogram
from .analysis.key import KeyEstimate, estimate_key, key_from_name
from .analysis.notes import SegmentConfig, segment_notes
from .analysis.pitch import PitchTrack, track_f0
from .audio.io import AudioBuffer
from .model import NoteSequence
from .process.psola import EpochTrack, detect_epochs, psola_modify
from .process.tuner import TuneConfig, TuneResult, tune


@dataclass
class TrackAnalysis:
    """Everything read off one vocal track, before any decision is made."""

    audio: np.ndarray                  # analysis-side mono
    sample_rate: int
    pitch: PitchTrack
    notes: NoteSequence
    spec: Spectrogram
    name: str = "track"
    key: KeyEstimate | None = None
    epochs: EpochTrack | None = None
    source: AudioBuffer | None = None

    @property
    def hop(self) -> int:
        return self.pitch.hop

    @property
    def duration(self) -> float:
        return self.audio.size / float(self.sample_rate)


def analyze_track(
    audio: np.ndarray | AudioBuffer,
    sample_rate: int | None = None,
    name: str = "track",
    hop: int = 256,
    fmin: float = 65.0,
    fmax: float = 1100.0,
    segment_config: SegmentConfig | None = None,
) -> TrackAnalysis:
    """Run pitch tracking and note segmentation on one track."""
    source = audio if isinstance(audio, AudioBuffer) else None
    if source is not None:
        mono, sample_rate = source.mono(), source.sample_rate
    else:
        mono = np.asarray(audio, dtype=np.float64)
        if mono.ndim > 1:
            mono = mono.mean(axis=1)
        if sample_rate is None:
            raise ValueError("sample_rate is required when passing raw samples")

    pitch = track_f0(mono, sample_rate, fmin=fmin, fmax=fmax, hop=hop)
    spec = spectrogram(mono, sample_rate, hop=hop)
    notes = segment_notes(pitch, mono, spec=spec, cfg=segment_config, source=name)
    return TrackAnalysis(
        audio=mono,
        sample_rate=sample_rate,
        pitch=pitch,
        notes=notes,
        spec=spec,
        name=name,
        source=source,
    )


def tune_track(
    analysis: TrackAnalysis,
    config: TuneConfig | None = None,
    key: KeyEstimate | str | None = None,
    render: bool = True,
) -> tuple[np.ndarray | None, TuneResult]:
    """Decide targets, build the correction curve, and render the audio."""
    config = config or TuneConfig()
    if isinstance(key, str):
        key = key_from_name(key)
    if key is None:
        key = analysis.key or estimate_key(analysis.notes)
    analysis.key = key

    result = tune(analysis.notes, analysis.pitch, key, config)
    if not render:
        return None, result

    audio = render_correction(analysis, result.ratio)
    return audio, result


def render_correction(analysis: TrackAnalysis, ratio: np.ndarray) -> np.ndarray:
    """Apply a frame-rate pitch ratio curve to the track's audio.

    Stereo and multichannel sources are shifted with one shared curve derived
    from the mono analysis, which keeps the channels phase-coherent.
    """
    if analysis.epochs is None:
        analysis.epochs = detect_epochs(
            analysis.audio, analysis.sample_rate, analysis.pitch.f0, analysis.hop
        )
    src = analysis.source
    if src is None or src.channels == 1:
        return psola_modify(
            analysis.audio, analysis.sample_rate, analysis.epochs, ratio, analysis.hop
        )

    out = np.empty_like(src.samples)
    for ch in range(src.channels):
        out[:, ch] = psola_modify(
            src.samples[:, ch], src.sample_rate, analysis.epochs, ratio, analysis.hop
        )
    return out
